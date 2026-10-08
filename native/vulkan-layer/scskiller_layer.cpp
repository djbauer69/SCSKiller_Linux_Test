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
#include <type_traits>

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
    PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout = nullptr;
    PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout = nullptr;
    PFN_vkCreatePipelineLayout CreatePipelineLayout = nullptr;
    PFN_vkDestroyPipelineLayout DestroyPipelineLayout = nullptr;
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

struct DescriptorLayoutKey
{
    VkDevice device{};
    VkDescriptorSetLayout layout{};

    bool operator==(const DescriptorLayoutKey& other) const
    {
        return device == other.device && layout == other.layout;
    }
};

struct DescriptorLayoutKeyHash
{
    size_t operator()(const DescriptorLayoutKey& key) const noexcept
    {
        const auto a = reinterpret_cast<uintptr_t>(key.device);
        const auto b = reinterpret_cast<uintptr_t>(key.layout);
        return static_cast<size_t>((a >> 4) ^ (b + 0x517cc1b727220a95ull + (a << 6) + (a >> 2)));
    }
};

struct PipelineLayoutKey
{
    VkDevice device{};
    VkPipelineLayout layout{};

    bool operator==(const PipelineLayoutKey& other) const
    {
        return device == other.device && layout == other.layout;
    }
};

struct PipelineLayoutKeyHash
{
    size_t operator()(const PipelineLayoutKey& key) const noexcept
    {
        const auto a = reinterpret_cast<uintptr_t>(key.device);
        const auto b = reinterpret_cast<uintptr_t>(key.layout);
        return static_cast<size_t>((a >> 4) ^ (b + 0x94d049bb133111ebull + (a << 6) + (a >> 2)));
    }
};

std::unordered_map<DescriptorLayoutKey, uint64_t, DescriptorLayoutKeyHash> g_descriptorLayoutHashes;
std::unordered_map<PipelineLayoutKey, uint64_t, PipelineLayoutKeyHash> g_pipelineLayoutHashes;

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

uint64_t HashBytes(const void* data, size_t size, uint64_t seed = 1469598103934665603ull)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = seed;
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

uint64_t HashCombine(uint64_t hash, uint64_t value)
{
    hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    return hash;
}

template <typename T>
uint64_t HandleBits(T handle)
{
    if constexpr (std::is_pointer_v<T>)
        return reinterpret_cast<uintptr_t>(handle);
    else
        return static_cast<uint64_t>(handle);
}

uint64_t HashDescriptorSetLayoutCreateInfo(const VkDescriptorSetLayoutCreateInfo* info)
{
    if (!info)
        return 0;

    uint64_t hash = HashCombine(1469598103934665603ull, info->flags);
    for (uint32_t i = 0; i < info->bindingCount; ++i)
    {
        const auto& binding = info->pBindings[i];
        hash = HashCombine(hash, binding.binding);
        hash = HashCombine(hash, binding.descriptorType);
        hash = HashCombine(hash, binding.descriptorCount);
        hash = HashCombine(hash, binding.stageFlags);
        hash = HashCombine(hash, binding.pImmutableSamplers ? binding.descriptorCount : 0);
        if (binding.pImmutableSamplers)
        {
            for (uint32_t sampler = 0; sampler < binding.descriptorCount; ++sampler)
                hash = HashCombine(hash, HandleBits(binding.pImmutableSamplers[sampler]));
        }
    }
    return hash;
}

uint64_t HashPipelineLayoutCreateInfo(
    VkDevice device,
    const VkPipelineLayoutCreateInfo* info)
{
    if (!info)
        return 0;

    uint64_t hash = HashCombine(1469598103934665603ull, info->flags);
    for (uint32_t i = 0; i < info->setLayoutCount; ++i)
    {
        uint64_t layoutHash = 0;
        std::lock_guard lock(g_mutex);
        auto it = g_descriptorLayoutHashes.find(
            DescriptorLayoutKey{device, info->pSetLayouts[i]});
        if (it != g_descriptorLayoutHashes.end())
            layoutHash = it->second;

        hash = HashCombine(hash, layoutHash);
    }

    for (uint32_t i = 0; i < info->pushConstantRangeCount; ++i)
    {
        const auto& range = info->pPushConstantRanges[i];
        hash = HashCombine(hash, range.stageFlags);
        hash = HashCombine(hash, range.offset);
        hash = HashCombine(hash, range.size);
    }

    return hash;
}

uint64_t HashSpecializationInfo(const VkSpecializationInfo* info)
{
    if (!info)
        return 0;

    uint64_t hash = HashCombine(1469598103934665603ull, info->mapEntryCount);
    for (uint32_t i = 0; i < info->mapEntryCount; ++i)
    {
        const auto& entry = info->pMapEntries[i];
        hash = HashCombine(hash, entry.constantID);
        hash = HashCombine(hash, entry.offset);
        hash = HashCombine(hash, entry.size);
        if (entry.size && info->pData)
            hash = HashBytes(static_cast<const uint8_t*>(info->pData) + entry.offset, entry.size, hash);
    }
    return hash;
}

const char* ShaderStageName(VkShaderStageFlagBits stage)
{
    switch (stage)
    {
    case VK_SHADER_STAGE_VERTEX_BIT: return "vertex";
    case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: return "tessellation_control";
    case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: return "tessellation_evaluation";
    case VK_SHADER_STAGE_GEOMETRY_BIT: return "geometry";
    case VK_SHADER_STAGE_FRAGMENT_BIT: return "fragment";
    case VK_SHADER_STAGE_COMPUTE_BIT: return "compute";
    default: return "other";
    }
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
            "{\"schema\":2,\"event\":\"graphics_pipeline_state\",\"sequence\":%llu,\"count\":%u,\"pipelines\":[",
            static_cast<unsigned long long>(sequence), count);

        for (uint32_t i = 0; i < count; ++i)
        {
            if (i) std::fputc(',', file);
            const auto& info = infos[i];

            uint64_t layoutHash = 0;
            {
                std::lock_guard lock(g_mutex);
                auto it = g_pipelineLayoutHashes.find(PipelineLayoutKey{device, info.layout});
                if (it != g_pipelineLayoutHashes.end())
                    layoutHash = it->second;
            }

            std::fprintf(file,
                "{\"layout_hash\":\"%016llx\",\"stage_count\":%u,\"stages\":[",
                static_cast<unsigned long long>(layoutHash), info.stageCount);

            for (uint32_t stage = 0; stage < info.stageCount; ++stage)
            {
                if (stage) std::fputc(',', file);
                const auto& state = info.pStages[stage];
                uint64_t shaderHash = 0;
                {
                    std::lock_guard lock(g_mutex);
                    auto it = g_shaderHashes.find(ShaderKey{device, state.module});
                    if (it != g_shaderHashes.end())
                        shaderHash = it->second;
                }
                const auto specializationHash = HashSpecializationInfo(state.pSpecializationInfo);
                std::fprintf(file,
                    "{\"stage\":\"%s\",\"module_hash\":\"%016llx\",\"specialization_hash\":\"%016llx\",\"specialization_data_size\":%zu}",
                    ShaderStageName(state.stage),
                    static_cast<unsigned long long>(shaderHash),
                    static_cast<unsigned long long>(specializationHash),
                    state.pSpecializationInfo ? state.pSpecializationInfo->dataSize : 0);
            }

            std::fprintf(file,
                "],\"flags\":%u,\"subpass\":%u}",
                info.flags, info.subpass);
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
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetPhysicalDeviceProcAddr(VkInstance, const char*);

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
    versionStruct->pfnGetPhysicalDeviceProcAddr = vkGetPhysicalDeviceProcAddr;
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
    dispatch.CreateDescriptorSetLayout = reinterpret_cast<PFN_vkCreateDescriptorSetLayout>(
        dispatch.GetDeviceProcAddr(*device, "vkCreateDescriptorSetLayout"));
    dispatch.DestroyDescriptorSetLayout = reinterpret_cast<PFN_vkDestroyDescriptorSetLayout>(
        dispatch.GetDeviceProcAddr(*device, "vkDestroyDescriptorSetLayout"));
    dispatch.CreatePipelineLayout = reinterpret_cast<PFN_vkCreatePipelineLayout>(
        dispatch.GetDeviceProcAddr(*device, "vkCreatePipelineLayout"));
    dispatch.DestroyPipelineLayout = reinterpret_cast<PFN_vkDestroyPipelineLayout>(
        dispatch.GetDeviceProcAddr(*device, "vkDestroyPipelineLayout"));
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

        for (auto layout = g_descriptorLayoutHashes.begin(); layout != g_descriptorLayoutHashes.end();)
        {
            if (layout->first.device == device)
                layout = g_descriptorLayoutHashes.erase(layout);
            else
                ++layout;
        }

        for (auto layout = g_pipelineLayoutHashes.begin(); layout != g_pipelineLayoutHashes.end();)
        {
            if (layout->first.device == device)
                layout = g_pipelineLayoutHashes.erase(layout);
            else
                ++layout;
        }
    }

    if (dispatch.DestroyDevice)
        dispatch.DestroyDevice(device, allocator);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDescriptorSetLayout(VkDevice device,
                            const VkDescriptorSetLayoutCreateInfo* createInfo,
                            const VkAllocationCallbacks* allocator,
                            VkDescriptorSetLayout* setLayout)
{
    DeviceDispatch dispatch{};
    {
        std::lock_guard lock(g_mutex);
        auto it = g_devices.find(device);
        if (it == g_devices.end())
            return VK_ERROR_DEVICE_LOST;
        dispatch = it->second;
    }

    if (!dispatch.CreateDescriptorSetLayout)
        return VK_ERROR_INITIALIZATION_FAILED;

    const uint64_t hash = HashDescriptorSetLayoutCreateInfo(createInfo);
    const VkResult result = dispatch.CreateDescriptorSetLayout(device, createInfo, allocator, setLayout);
    if (result == VK_SUCCESS && setLayout && hash)
    {
        {
            std::lock_guard lock(g_mutex);
            g_descriptorLayoutHashes[DescriptorLayoutKey{device, *setLayout}] = hash;
        }

        if (RecordingEnabled())
        {
            const auto sequence = g_sequence.fetch_add(1);
            if (const char* path = RecordingPath())
            {
                if (std::FILE* file = std::fopen(path, "ab"))
                {
                    std::fprintf(file,
                        "{\"schema\":2,\"event\":\"descriptor_set_layout_create\",\"sequence\":%llu,\"hash\":\"%016llx\",\"flags\":%u,\"bindings\":[",
                        static_cast<unsigned long long>(sequence),
                        static_cast<unsigned long long>(hash),
                        createInfo ? createInfo->flags : 0);
                    if (createInfo)
                    {
                        for (uint32_t i = 0; i < createInfo->bindingCount; ++i)
                        {
                            if (i) std::fputc(',', file);
                            const auto& binding = createInfo->pBindings[i];
                            std::fprintf(file,
                                "{\"binding\":%u,\"descriptor_type\":%u,\"descriptor_count\":%u,\"stage_flags\":%u,\"immutable_sampler_count\":%u}",
                                binding.binding,
                                binding.descriptorType,
                                binding.descriptorCount,
                                binding.stageFlags,
                                binding.pImmutableSamplers ? binding.descriptorCount : 0);
                        }
                    }
                    std::fprintf(file, "]}\n");
                    std::fclose(file);
                }
            }
        }
    }

    return result;
}

extern "C" VKAPI_ATTR void VKAPI_CALL
vkDestroyDescriptorSetLayout(VkDevice device,
                             VkDescriptorSetLayout setLayout,
                             const VkAllocationCallbacks* allocator)
{
    DeviceDispatch dispatch{};
    {
        std::lock_guard lock(g_mutex);
        auto it = g_devices.find(device);
        if (it == g_devices.end())
            return;

        dispatch = it->second;
        g_descriptorLayoutHashes.erase(DescriptorLayoutKey{device, setLayout});
    }

    if (dispatch.DestroyDescriptorSetLayout)
        dispatch.DestroyDescriptorSetLayout(device, setLayout, allocator);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkCreatePipelineLayout(VkDevice device,
                       const VkPipelineLayoutCreateInfo* createInfo,
                       const VkAllocationCallbacks* allocator,
                       VkPipelineLayout* pipelineLayout)
{
    DeviceDispatch dispatch{};
    {
        std::lock_guard lock(g_mutex);
        auto it = g_devices.find(device);
        if (it == g_devices.end())
            return VK_ERROR_DEVICE_LOST;
        dispatch = it->second;
    }

    if (!dispatch.CreatePipelineLayout)
        return VK_ERROR_INITIALIZATION_FAILED;

    const uint64_t hash = HashPipelineLayoutCreateInfo(device, createInfo);
    const VkResult result = dispatch.CreatePipelineLayout(device, createInfo, allocator, pipelineLayout);
    if (result == VK_SUCCESS && pipelineLayout && hash)
    {
        {
            std::lock_guard lock(g_mutex);
            g_pipelineLayoutHashes[PipelineLayoutKey{device, *pipelineLayout}] = hash;
        }

        if (RecordingEnabled())
        {
            const auto sequence = g_sequence.fetch_add(1);
            if (const char* path = RecordingPath())
            {
                if (std::FILE* file = std::fopen(path, "ab"))
                {
                    std::fprintf(file,
                        "{\"schema\":2,\"event\":\"pipeline_layout_create\",\"sequence\":%llu,\"hash\":\"%016llx\",\"flags\":%u,\"set_layouts\":[",
                        static_cast<unsigned long long>(sequence),
                        static_cast<unsigned long long>(hash),
                        createInfo ? createInfo->flags : 0);
                    if (createInfo)
                    {
                        for (uint32_t i = 0; i < createInfo->setLayoutCount; ++i)
                        {
                            if (i) std::fputc(',', file);
                            uint64_t layoutHash = 0;
                            {
                                std::lock_guard lock(g_mutex);
                                auto it = g_descriptorLayoutHashes.find(
                                    DescriptorLayoutKey{device, createInfo->pSetLayouts[i]});
                                if (it != g_descriptorLayoutHashes.end())
                                    layoutHash = it->second;
                            }
                            std::fprintf(file, "\"%016llx\"",
                                         static_cast<unsigned long long>(layoutHash));
                        }
                    }
                    std::fprintf(file, "],\"push_constants\":[");
                    if (createInfo)
                    {
                        for (uint32_t i = 0; i < createInfo->pushConstantRangeCount; ++i)
                        {
                            if (i) std::fputc(',', file);
                            const auto& range = createInfo->pPushConstantRanges[i];
                            std::fprintf(file,
                                "{\"stage_flags\":%u,\"offset\":%u,\"size\":%u}",
                                range.stageFlags, range.offset, range.size);
                        }
                    }
                    std::fprintf(file, "]}\n");
                    std::fclose(file);
                }
            }
        }
    }

    return result;
}

extern "C" VKAPI_ATTR void VKAPI_CALL
vkDestroyPipelineLayout(VkDevice device,
                        VkPipelineLayout pipelineLayout,
                        const VkAllocationCallbacks* allocator)
{
    DeviceDispatch dispatch{};
    {
        std::lock_guard lock(g_mutex);
        auto it = g_devices.find(device);
        if (it == g_devices.end())
            return;

        dispatch = it->second;
        g_pipelineLayoutHashes.erase(PipelineLayoutKey{device, pipelineLayout});
    }

    if (dispatch.DestroyPipelineLayout)
        dispatch.DestroyPipelineLayout(device, pipelineLayout, allocator);
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

    const auto sequence = g_sequence.fetch_add(1);
    RecordCount("compute_pipeline_create", sequence, createInfoCount);
    if (RecordingEnabled() && createInfos)
    {
        if (const char* path = RecordingPath())
        {
            if (std::FILE* file = std::fopen(path, "ab"))
            {
                std::fprintf(file,
                    "{\"schema\":2,\"event\":\"compute_pipeline_state\",\"sequence\":%llu,\"count\":%u,\"pipelines\":[",
                    static_cast<unsigned long long>(sequence), createInfoCount);
                for (uint32_t i = 0; i < createInfoCount; ++i)
                {
                    if (i) std::fputc(',', file);
                    const auto& info = createInfos[i];
                    uint64_t shaderHash = 0;
                    {
                        std::lock_guard lock(g_mutex);
                        auto it = g_shaderHashes.find(ShaderKey{device, info.stage.module});
                        if (it != g_shaderHashes.end())
                            shaderHash = it->second;
                    }
                    uint64_t layoutHash = 0;
                    {
                        std::lock_guard lock(g_mutex);
                        auto it = g_pipelineLayoutHashes.find(PipelineLayoutKey{device, info.layout});
                        if (it != g_pipelineLayoutHashes.end())
                            layoutHash = it->second;
                    }
                    const auto specializationHash = HashSpecializationInfo(info.stage.pSpecializationInfo);
                    std::fprintf(file,
                        "{\"layout_hash\":\"%016llx\",\"module_hash\":\"%016llx\",\"specialization_hash\":\"%016llx\",\"specialization_data_size\":%zu,\"stage\":\"%s\",\"flags\":%u}",
                        static_cast<unsigned long long>(layoutHash),
                        static_cast<unsigned long long>(shaderHash),
                        static_cast<unsigned long long>(specializationHash),
                        info.stage.pSpecializationInfo ? info.stage.pSpecializationInfo->dataSize : 0,
                        ShaderStageName(info.stage.stage),
                        info.flags);
                }
                std::fprintf(file, "]}\n");
                std::fclose(file);
            }
        }
    }

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
    if (std::strcmp(name, "vkCreateDescriptorSetLayout") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateDescriptorSetLayout);
    if (std::strcmp(name, "vkDestroyDescriptorSetLayout") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyDescriptorSetLayout);
    if (std::strcmp(name, "vkCreatePipelineLayout") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreatePipelineLayout);
    if (std::strcmp(name, "vkDestroyPipelineLayout") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyPipelineLayout);
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


extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetPhysicalDeviceProcAddr(VkInstance instance, const char* name)
{
    if (!name || !g_nextInstanceProcAddr)
        return nullptr;

    return g_nextInstanceProcAddr(instance, name);
}
