#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

namespace
{
constexpr char kLayerName[] = "VK_LAYER_SCSKILLER";
constexpr char kRecordEnv[] = "SCSKILLER_VK_RECORD";
constexpr char kRecordFileEnv[] = "SCSKILLER_VK_RECORD_FILE";

struct ShaderKey
{
    VkDevice device{};
    VkShaderModule module{};

    bool operator==(const ShaderKey& other) const
    {
        return device == other.device && module == other.module;
    }
};

struct ShaderKeyHash
{
    size_t operator()(const ShaderKey& key) const noexcept
    {
        const auto a = reinterpret_cast<uintptr_t>(key.device);
        const auto b = reinterpret_cast<uintptr_t>(key.module);
        return static_cast<size_t>((a >> 4) ^ (b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2)));
    }
};

struct DeviceDispatch
{
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr = nullptr;
    PFN_vkDestroyDevice DestroyDevice = nullptr;
    PFN_vkCreateShaderModule CreateShaderModule = nullptr;
    PFN_vkDestroyShaderModule DestroyShaderModule = nullptr;
    PFN_vkCreateGraphicsPipelines CreateGraphicsPipelines = nullptr;
    PFN_vkCreateComputePipelines CreateComputePipelines = nullptr;
    PFN_vkCreateRayTracingPipelinesKHR CreateRayTracingPipelinesKHR = nullptr;
    PFN_vkCreatePipelineCache CreatePipelineCache = nullptr;
    PFN_vkGetPipelineCacheData GetPipelineCacheData = nullptr;
    PFN_vkDestroyPipelineCache DestroyPipelineCache = nullptr;
};

std::mutex g_mutex;
std::unordered_map<VkDevice, DeviceDispatch> g_devices;
std::unordered_map<ShaderKey, uint64_t, ShaderKeyHash> g_shaderHashes;
std::atomic<uint64_t> g_sequence{1};
PFN_vkGetInstanceProcAddr g_nextInstanceProcAddr = nullptr;

bool RecordingEnabled()
{
    const char* value = std::getenv(kRecordEnv);
    return value && value[0] && std::strcmp(value, "0") != 0;
}

const char* RecordingPath()
{
    const char* path = std::getenv(kRecordFileEnv);
    return path && path[0] ? path : nullptr;
}

uint64_t HashWords(const uint32_t* words, size_t count)
{
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < count; ++i)
    {
        hash ^= words[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string Base64(const uint8_t* data, size_t size)
{
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((size + 2) / 3) * 4);
    for (size_t i = 0; i < size; i += 3)
    {
        const uint32_t a = data[i];
        const uint32_t b = i + 1 < size ? data[i + 1] : 0;
        const uint32_t d = i + 2 < size ? data[i + 2] : 0;
        const uint32_t triple = (a << 16) | (b << 8) | d;
        out.push_back(table[(triple >> 18) & 63]);
        out.push_back(table[(triple >> 12) & 63]);
        out.push_back(i + 1 < size ? table[(triple >> 6) & 63] : '=');
        out.push_back(i + 2 < size ? table[triple & 63] : '=');
    }
    return out;
}

void RecordShaderCode(uint64_t sequence, uint64_t hash, const uint32_t* words, size_t wordCount)
{
    if (!RecordingEnabled() || !words || wordCount == 0)
        return;

    const char* path = RecordingPath();
    if (!path)
        return;

    const auto encoded = Base64(reinterpret_cast<const uint8_t*>(words), wordCount * sizeof(uint32_t));
    if (std::FILE* file = std::fopen(path, "ab"))
    {
        std::fprintf(file,
            "{\"schema\":2,\"event\":\"shader_module_code\",\"sequence\":%llu,\"hash\":\"%016llx\",\"code_base64\":\"%s\"}\n",
            static_cast<unsigned long long>(sequence),
            static_cast<unsigned long long>(hash),
            encoded.c_str());
        std::fclose(file);
    }
}

void RecordShader(const char* event, uint64_t sequence, uint64_t hash, size_t wordCount)
{
    if (!RecordingEnabled())
        return;

    const char* path = RecordingPath();
    if (!path)
        return;

    if (std::FILE* file = std::fopen(path, "ab"))
    {
        std::fprintf(
            file,
            "{\"schema\":1,\"event\":\"%s\",\"sequence\":%llu,\"code_words\":%zu,\"hash\":\"%016llx\"}\n",
            event,
            static_cast<unsigned long long>(sequence),
            wordCount,
            static_cast<unsigned long long>(hash));
        std::fclose(file);
    }
}


void RecordCacheSnapshot(const char* event, uint64_t sequence, const void* data, size_t size)
{
    if (!RecordingEnabled() || !data || size == 0)
        return;

    const char* base = RecordingPath();
    if (!base)
        return;

    char path[4096]{};
    std::snprintf(path, sizeof(path), "%s.cache.%llu.bin",
                  base, static_cast<unsigned long long>(sequence));

    if (std::FILE* file = std::fopen(path, "wb"))
    {
        const size_t written = std::fwrite(data, 1, size, file);
        std::fclose(file);

        if (written == size)
        {
            if (std::FILE* log = std::fopen(base, "ab"))
            {
                std::fprintf(log,
                    "{\"schema\":1,\"event\":\"%s\",\"sequence\":%llu,\"size\":%zu,\"path\":\"%s\"}\n",
                    event,
                    static_cast<unsigned long long>(sequence),
                    size,
                    path);
                std::fclose(log);
            }
        }
    }
}

void RecordGraphicsStages(VkDevice device, uint64_t sequence,
                          uint32_t count,
                          const VkGraphicsPipelineCreateInfo* infos)
{
    if (!RecordingEnabled() || !infos)
        return;

    const char* path = RecordingPath();
    if (!path)
        return;

    if (std::FILE* file = std::fopen(path, "ab"))
    {
        std::fprintf(file,
            "{\"schema\":1,\"event\":\"graphics_pipeline_state\",\"sequence\":%llu,\"count\":%u,\"pipelines\":[",
            static_cast<unsigned long long>(sequence), count);

        for (uint32_t i = 0; i < count; ++i)
        {
            if (i) std::fputc(',', file);
            std::fprintf(file, "{\"stage_hashes\":[");
            for (uint32_t s = 0; s < infos[i].stageCount; ++s)
            {
                if (s) std::fputc(',', file);
                uint64_t hash = 0;
                if (infos[i].pStages[s].module != VK_NULL_HANDLE)
                {
                    std::lock_guard lock(g_mutex);
                    auto it = g_shaderHashes.find(ShaderKey{device, infos[i].pStages[s].module});
                    if (it != g_shaderHashes.end())
                        hash = it->second;
                }
                std::fprintf(file, "\"%016llx\"", static_cast<unsigned long long>(hash));
            }
            std::fprintf(file, "],\"flags\":%u,\"subpass\":%u}",
                         infos[i].flags, infos[i].subpass);
        }

        std::fprintf(file, "]}\n");
        std::fclose(file);
    }
}

void RecordCount(const char* event, uint64_t sequence, uint32_t count)
{
    if (!RecordingEnabled())
        return;

    const char* path = RecordingPath();
    if (!path)
        return;

    if (std::FILE* file = std::fopen(path, "ab"))
    {
        std::fprintf(
            file,
            "{\"schema\":1,\"event\":\"%s\",\"sequence\":%llu,\"count\":%u}\n",
            event,
            static_cast<unsigned long long>(sequence),
            count);
        std::fclose(file);
    }
}

VkLayerInstanceCreateInfo* FindInstanceLinkInfo(const VkInstanceCreateInfo* createInfo)
{
    auto* current = createInfo
        ? reinterpret_cast<const VkBaseInStructure*>(createInfo->pNext)
        : nullptr;

    while (current)
    {
        if (current->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO)
        {
            auto* info = reinterpret_cast<VkLayerInstanceCreateInfo*>(
                const_cast<VkBaseInStructure*>(current));
            if (info->function == VK_LAYER_LINK_INFO)
                return info;
        }
        current = current->pNext;
    }

    return nullptr;
}

VkLayerDeviceCreateInfo* FindDeviceLinkInfo(const VkDeviceCreateInfo* createInfo)
{
    auto* current = createInfo
        ? reinterpret_cast<const VkBaseInStructure*>(createInfo->pNext)
        : nullptr;

    while (current)
    {
        if (current->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO)
        {
            auto* info = reinterpret_cast<VkLayerDeviceCreateInfo*>(
                const_cast<VkBaseInStructure*>(current));
            if (info->function == VK_LAYER_LINK_INFO)
                return info;
        }
        current = current->pNext;
    }

    return nullptr;
}
}

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance, const char*);
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice, const char*);

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* versionStruct)
{
    if (!versionStruct)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (versionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (versionStruct->loaderLayerInterfaceVersion < 2)
        return VK_ERROR_INITIALIZATION_FAILED;

    versionStruct->loaderLayerInterfaceVersion = 2;
    versionStruct->pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    versionStruct->pfnGetDeviceProcAddr = vkGetDeviceProcAddr;
    versionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;
    return VK_SUCCESS;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceLayerProperties(uint32_t* propertyCount, VkLayerProperties* properties)
{
    if (!propertyCount)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (!properties)
    {
        *propertyCount = 1;
        return VK_SUCCESS;
    }

    if (*propertyCount == 0)
        return VK_INCOMPLETE;

    VkLayerProperties layer{};
    std::strncpy(layer.layerName, kLayerName, VK_MAX_EXTENSION_NAME_SIZE - 1);
    std::strncpy(layer.description, "SCSKiller experimental Vulkan pipeline recorder", VK_MAX_DESCRIPTION_SIZE - 1);
    layer.specVersion = VK_API_VERSION_1_0;
    layer.implementationVersion = 1;
    properties[0] = layer;
    *propertyCount = 1;
    return VK_SUCCESS;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkCreateInstance(const VkInstanceCreateInfo* createInfo,
                 const VkAllocationCallbacks* allocator,
                 VkInstance* instance)
{
    auto* linkInfo = FindInstanceLinkInfo(createInfo);
    if (!linkInfo || !linkInfo->u.pLayerInfo || !instance)
        return VK_ERROR_INITIALIZATION_FAILED;

    auto next = linkInfo->u.pLayerInfo;
    linkInfo->u.pLayerInfo = next->pNext;

    g_nextInstanceProcAddr = next->pfnNextGetInstanceProcAddr;

    auto createNext = reinterpret_cast<PFN_vkCreateInstance>(
        next->pfnNextGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));

    if (!createNext)
        return VK_ERROR_INITIALIZATION_FAILED;

    return createNext(createInfo, allocator, instance);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDevice(VkPhysicalDevice physicalDevice,
               const VkDeviceCreateInfo* createInfo,
               const VkAllocationCallbacks* allocator,
               VkDevice* device)
{
    auto* linkInfo = FindDeviceLinkInfo(createInfo);
    if (!linkInfo || !linkInfo->u.pLayerInfo || !device)
        return VK_ERROR_INITIALIZATION_FAILED;

    auto next = linkInfo->u.pLayerInfo;
    linkInfo->u.pLayerInfo = next->pNext;

    auto createNext = reinterpret_cast<PFN_vkCreateDevice>(
        next->pfnNextGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateDevice"));

    if (!createNext)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkResult result = createNext(physicalDevice, createInfo, allocator, device);
    if (result != VK_SUCCESS)
        return result;

    DeviceDispatch dispatch{};
    dispatch.GetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        next->pfnNextGetInstanceProcAddr(VK_NULL_HANDLE, "vkGetDeviceProcAddr"));

    if (!dispatch.GetDeviceProcAddr)
        return result;

    dispatch.DestroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(
        dispatch.GetDeviceProcAddr(*device, "vkDestroyDevice"));
    dispatch.CreateShaderModule = reinterpret_cast<PFN_vkCreateShaderModule>(
        dispatch.GetDeviceProcAddr(*device, "vkCreateShaderModule"));
    dispatch.DestroyShaderModule = reinterpret_cast<PFN_vkDestroyShaderModule>(
        dispatch.GetDeviceProcAddr(*device, "vkDestroyShaderModule"));
    dispatch.CreateGraphicsPipelines = reinterpret_cast<PFN_vkCreateGraphicsPipelines>(
        dispatch.GetDeviceProcAddr(*device, "vkCreateGraphicsPipelines"));
    dispatch.CreateComputePipelines = reinterpret_cast<PFN_vkCreateComputePipelines>(
        dispatch.GetDeviceProcAddr(*device, "vkCreateComputePipelines"));
    dispatch.CreateRayTracingPipelinesKHR = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(
        dispatch.GetDeviceProcAddr(*device, "vkCreateRayTracingPipelinesKHR"));
    dispatch.CreatePipelineCache = reinterpret_cast<PFN_vkCreatePipelineCache>(
        dispatch.GetDeviceProcAddr(*device, "vkCreatePipelineCache"));
    dispatch.GetPipelineCacheData = reinterpret_cast<PFN_vkGetPipelineCacheData>(
        dispatch.GetDeviceProcAddr(*device, "vkGetPipelineCacheData"));
    dispatch.DestroyPipelineCache = reinterpret_cast<PFN_vkDestroyPipelineCache>(
        dispatch.GetDeviceProcAddr(*device, "vkDestroyPipelineCache"));

    std::lock_guard lock(g_mutex);
    g_devices.emplace(*device, dispatch);
    return result;
}

extern "C" VKAPI_ATTR void VKAPI_CALL
vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator)
{
    DeviceDispatch dispatch{};
    {
        std::lock_guard lock(g_mutex);
        auto it = g_devices.find(device);
        if (it == g_devices.end())
            return;

        dispatch = it->second;
        g_devices.erase(it);

        for (auto shader = g_shaderHashes.begin(); shader != g_shaderHashes.end();)
        {
            if (shader->first.device == device)
                shader = g_shaderHashes.erase(shader);
            else
                ++shader;
        }
    }

    if (dispatch.DestroyDevice)
        dispatch.DestroyDevice(device, allocator);
}

extern "C" VKAPI_ATTR void VKAPI_CALL
vkDestroyShaderModule(VkDevice device,
                      VkShaderModule shaderModule,
                      const VkAllocationCallbacks* allocator)
{
    DeviceDispatch dispatch{};
    {
        std::lock_guard lock(g_mutex);
        auto it = g_devices.find(device);
        if (it == g_devices.end())
            return;

        dispatch = it->second;
        g_shaderHashes.erase(ShaderKey{device, shaderModule});
    }

    if (dispatch.DestroyShaderModule)
        dispatch.DestroyShaderModule(device, shaderModule, allocator);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkCreateShaderModule(VkDevice device,
                     const VkShaderModuleCreateInfo* createInfo,
                     const VkAllocationCallbacks* allocator,
                     VkShaderModule* shaderModule)
{
    DeviceDispatch dispatch{};
    {
        std::lock_guard lock(g_mutex);
        auto it = g_devices.find(device);
        if (it == g_devices.end())
            return VK_ERROR_DEVICE_LOST;

        dispatch = it->second;
    }

    if (!dispatch.CreateShaderModule)
        return VK_ERROR_INITIALIZATION_FAILED;

    uint64_t hash = 0;
    size_t wordCount = 0;
    if (createInfo && createInfo->pCode && createInfo->codeSize >= sizeof(uint32_t))
    {
        wordCount = createInfo->codeSize / sizeof(uint32_t);
        hash = HashWords(createInfo->pCode, wordCount);
    }

    VkResult result = dispatch.CreateShaderModule(device, createInfo, allocator, shaderModule);

    if (result == VK_SUCCESS && hash != 0 && shaderModule)
    {
        std::lock_guard lock(g_mutex);
        g_shaderHashes[ShaderKey{device, *shaderModule}] = hash;
    }

    if (hash != 0)
    {
        const auto sequence = g_sequence.fetch_add(1);
        RecordShader("shader_module_create", sequence, hash, wordCount);
        RecordShaderCode(sequence, hash, createInfo->pCode, wordCount);
    }

    return result;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkCreateGraphicsPipelines(VkDevice device,
                          VkPipelineCache pipelineCache,
                          uint32_t createInfoCount,
                          const VkGraphicsPipelineCreateInfo* createInfos,
                          const VkAllocationCallbacks* allocator,
                          VkPipeline* pipelines)
{
    DeviceDispatch dispatch{};
    {
        std::lock_guard lock(g_mutex);
        auto it = g_devices.find(device);
        if (it == g_devices.end())
            return VK_ERROR_DEVICE_LOST;

        dispatch = it->second;
    }

    if (!dispatch.CreateGraphicsPipelines)
        return VK_ERROR_INITIALIZATION_FAILED;

    const auto sequence = g_sequence.fetch_add(1);
    RecordCount("graphics_pipeline_create", sequence, createInfoCount);
    RecordGraphicsStages(device, sequence, createInfoCount, createInfos);

    return dispatch.CreateGraphicsPipelines(
        device, pipelineCache, createInfoCount, createInfos, allocator, pipelines);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkCreateComputePipelines(VkDevice device,
                         VkPipelineCache pipelineCache,
                         uint32_t createInfoCount,
                         const VkComputePipelineCreateInfo* createInfos,
                         const VkAllocationCallbacks* allocator,
                         VkPipeline* pipelines)
{
    DeviceDispatch dispatch{};
    {
        std::lock_guard lock(g_mutex);
        auto it = g_devices.find(device);
        if (it == g_devices.end())
            return VK_ERROR_DEVICE_LOST;

        dispatch = it->second;
    }

    if (!dispatch.CreateComputePipelines)
        return VK_ERROR_INITIALIZATION_FAILED;

    RecordCount(
        "compute_pipeline_create",
        g_sequence.fetch_add(1),
        createInfoCount);

    return dispatch.CreateComputePipelines(
        device, pipelineCache, createInfoCount, createInfos, allocator, pipelines);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkCreateRayTracingPipelinesKHR(VkDevice device,
                               VkDeferredOperationKHR deferredOperation,
                               VkPipelineCache pipelineCache,
                               uint32_t createInfoCount,
                               const VkRayTracingPipelineCreateInfoKHR* createInfos,
                               const VkAllocationCallbacks* allocator,
                               VkPipeline* pipelines)
{
    DeviceDispatch dispatch{};
    {
        std::lock_guard lock(g_mutex);
        auto it = g_devices.find(device);
        if (it == g_devices.end())
            return VK_ERROR_DEVICE_LOST;

        dispatch = it->second;
    }

    if (!dispatch.CreateRayTracingPipelinesKHR)
        return VK_ERROR_EXTENSION_NOT_PRESENT;

    RecordCount(
        "ray_tracing_pipeline_create",
        g_sequence.fetch_add(1),
        createInfoCount);

    return dispatch.CreateRayTracingPipelinesKHR(
        device,
        deferredOperation,
        pipelineCache,
        createInfoCount,
        createInfos,
        allocator,
        pipelines);
}


extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkCreatePipelineCache(VkDevice device,
                      const VkPipelineCacheCreateInfo* createInfo,
                      const VkAllocationCallbacks* allocator,
                      VkPipelineCache* pipelineCache)
{
    DeviceDispatch dispatch{};
    {
        std::lock_guard lock(g_mutex);
        auto it = g_devices.find(device);
        if (it == g_devices.end())
            return VK_ERROR_DEVICE_LOST;
        dispatch = it->second;
    }

    if (!dispatch.CreatePipelineCache)
        return VK_ERROR_INITIALIZATION_FAILED;

    const char* replayPath = std::getenv("SCSKILLER_VK_REPLAY_CACHE");
    if (!replayPath || !replayPath[0] || !createInfo || createInfo->initialDataSize != 0)
        return dispatch.CreatePipelineCache(device, createInfo, allocator, pipelineCache);

    std::FILE* file = std::fopen(replayPath, "rb");
    if (!file)
        return dispatch.CreatePipelineCache(device, createInfo, allocator, pipelineCache);

    std::fseek(file, 0, SEEK_END);
    const long length = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);

    if (length <= 0)
    {
        std::fclose(file);
        return dispatch.CreatePipelineCache(device, createInfo, allocator, pipelineCache);
    }

    std::string data(static_cast<size_t>(length), '\0');
    const size_t read = std::fread(data.data(), 1, data.size(), file);
    std::fclose(file);

    if (read != data.size())
        return dispatch.CreatePipelineCache(device, createInfo, allocator, pipelineCache);

    VkPipelineCacheCreateInfo replayInfo = *createInfo;
    replayInfo.initialDataSize = data.size();
    replayInfo.pInitialData = data.data();

    VkResult result = dispatch.CreatePipelineCache(device, &replayInfo, allocator, pipelineCache);
    if (result == VK_SUCCESS)
        RecordCount("pipeline_cache_replay", g_sequence.fetch_add(1), 1);

    return result;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkGetPipelineCacheData(VkDevice device,
                       VkPipelineCache pipelineCache,
                       size_t* pDataSize,
                       void* pData)
{
    DeviceDispatch dispatch{};
    {
        std::lock_guard lock(g_mutex);
        auto it = g_devices.find(device);
        if (it == g_devices.end())
            return VK_ERROR_DEVICE_LOST;
        dispatch = it->second;
    }

    if (!dispatch.GetPipelineCacheData)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkResult result = dispatch.GetPipelineCacheData(device, pipelineCache, pDataSize, pData);
    if (result == VK_SUCCESS && pData && pDataSize && *pDataSize)
        RecordCacheSnapshot("pipeline_cache_snapshot", g_sequence.fetch_add(1), pData, *pDataSize);

    return result;
}

extern "C" VKAPI_ATTR void VKAPI_CALL
vkDestroyPipelineCache(VkDevice device,
                        VkPipelineCache pipelineCache,
                        const VkAllocationCallbacks* allocator)
{
    DeviceDispatch dispatch{};
    {
        std::lock_guard lock(g_mutex);
        auto it = g_devices.find(device);
        if (it == g_devices.end())
            return;
        dispatch = it->second;
    }

    if (dispatch.DestroyPipelineCache)
        dispatch.DestroyPipelineCache(device, pipelineCache, allocator);
}

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(VkDevice device, const char* name)
{
    if (!name)
        return nullptr;

    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    if (std::strcmp(name, "vkDestroyDevice") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyDevice);
    if (std::strcmp(name, "vkCreateGraphicsPipelines") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateGraphicsPipelines);
    if (std::strcmp(name, "vkCreateComputePipelines") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateComputePipelines);
    if (std::strcmp(name, "vkCreateRayTracingPipelinesKHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateRayTracingPipelinesKHR);
    if (std::strcmp(name, "vkCreateShaderModule") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateShaderModule);
    if (std::strcmp(name, "vkDestroyShaderModule") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyShaderModule);
    if (std::strcmp(name, "vkCreatePipelineCache") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreatePipelineCache);
    if (std::strcmp(name, "vkGetPipelineCacheData") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetPipelineCacheData);
    if (std::strcmp(name, "vkDestroyPipelineCache") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyPipelineCache);

    std::lock_guard lock(g_mutex);
    auto it = g_devices.find(device);
    if (it == g_devices.end() || !it->second.GetDeviceProcAddr)
        return nullptr;

    return it->second.GetDeviceProcAddr(device, name);
}

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char* name)
{
    if (!name)
        return nullptr;

    if (std::strcmp(name, "vkNegotiateLoaderLayerInterfaceVersion") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkNegotiateLoaderLayerInterfaceVersion);
    if (std::strcmp(name, "vkGetInstanceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetInstanceProcAddr);
    if (std::strcmp(name, "vkCreateInstance") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateInstance);
    if (std::strcmp(name, "vkCreateDevice") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateDevice);
    if (std::strcmp(name, "vkEnumerateInstanceLayerProperties") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceLayerProperties);

    return g_nextInstanceProcAddr ? g_nextInstanceProcAddr(instance, name) : nullptr;
}
