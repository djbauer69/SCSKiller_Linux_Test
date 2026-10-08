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
std::unordered_map<VkShaderModule, uint64_t> g_shaderHashes;
std::atomic<uint64_t> g_sequence{1};
PFN_vkGetInstanceProcAddr g_nextInstanceProcAddr = nullptr;

bool RecordingEnabled()
{
    const char* value = std::getenv(kRecordEnv);
    return value && value[0] && std::strcmp(value, "0") != 0;
}

uint64_t HashWords(const uint32_t* words, size_t count)\n{\n    uint64_t hash = 1469598103934665603ull;\n    for (size_t i = 0; i < count; ++i) {\n        hash ^= words[i];\n        hash *= 1099511628211ull;\n    }\n    return hash;\n}\n\nvoid RecordLine(const char* event, uint64_t sequence, uint32_t count)
{
    if (!RecordingEnabled())
        return;

    const char* path = std::getenv("SCSKILLER_VK_RECORD_FILE");
    if (!path || !path[0])
        return;

    std::FILE* file = std::fopen(path, "ab");
    if (!file)
        return;

    std::fprintf(file, "{\"event\":\"%s\",\"sequence\":%llu,\"count\":%u}\n",
                 event,
                 static_cast<unsigned long long>(sequence),
                 count);
    std::fclose(file);
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
    }

    if (dispatch.DestroyDevice)
        dispatch.DestroyDevice(device, allocator);
}

extern "C" VKAPI_ATTR void VKAPI_CALL
vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks* allocator)
{
    DeviceDispatch dispatch{};
    {
        std::lock_guard lock(g_mutex);
        auto it = g_devices.find(device);
        if (it == g_devices.end()) return;
        dispatch = it->second;
        g_shaderHashes.erase(shaderModule);
    }
    if (dispatch.DestroyShaderModule)
        dispatch.DestroyShaderModule(device, shaderModule, allocator);
}
extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* createInfo, const VkAllocationCallbacks* allocator, VkShaderModule* shaderModule)
{
    DeviceDispatch dispatch{};
    {
        std::lock_guard lock(g_mutex);
        auto it = g_devices.find(device);
        if (it == g_devices.end()) return VK_ERROR_DEVICE_LOST;
        dispatch = it->second;
    }
    if (!dispatch.CreateShaderModule) return VK_ERROR_INITIALIZATION_FAILED;
    if (createInfo && createInfo->pCode && createInfo->codeSize >= sizeof(uint32_t)) {
        const auto hash = HashWords(createInfo->pCode, createInfo->codeSize / sizeof(uint32_t));
        if (RecordingEnabled()) {
            const char* path = std::getenv("SCSKILLER_VK_RECORD_FILE");
            if (path && path[0]) {
                if (std::FILE* file = std::fopen(path, "ab")) {
                    const auto sequence = g_sequence.fetch_add(1);
                    std::fprintf(file, "{\"event\":\"shader_module_create\",\"sequence\":%llu,\"code_words\":%zu,\"hash\":\"%016llx\"}\n", static_cast<unsigned long long>(sequence), createInfo->codeSize / sizeof(uint32_t), static_cast<unsigned long long>(hash));
                    std::fclose(file);
                }
            }
        }
    }
    VkResult result = dispatch.CreateShaderModule(device, createInfo, allocator, shaderModule);
    if (result == VK_SUCCESS && createInfo && createInfo->pCode && createInfo->codeSize >= sizeof(uint32_t) && shaderModule) {
        std::lock_guard lock(g_mutex);
        g_shaderHashes[*shaderModule] = HashWords(createInfo->pCode, createInfo->codeSize / sizeof(uint32_t));
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
    RecordLine("graphics_pipeline_create", sequence, createInfoCount);

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
    RecordLine("compute_pipeline_create", sequence, createInfoCount);

    return dispatch.CreateComputePipelines(
        device, pipelineCache, createInfoCount, createInfos, allocator, pipelines);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkGetDeviceProcAddr(VkDevice device, const char* name)
{
    if (!name)
        return nullptr;

    if (std::strcmp(name, "vkCreateDevice") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateDevice);
    if (std::strcmp(name, "vkDestroyDevice") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyDevice);
    if (std::strcmp(name, "vkCreateGraphicsPipelines") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateGraphicsPipelines);
    if (std::strcmp(name, "vkCreateComputePipelines") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateComputePipelines);
    if (std::strcmp(name, "vkCreateShaderModule") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateShaderModule);
    if (std::strcmp(name, "vkDestroyShaderModule") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyShaderModule);
    if (std::strcmp(name, "vkCreateRayTracingPipelinesKHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateRayTracingPipelinesKHR);

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

    if (std::strcmp(name, "vkCreateInstance") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateInstance);
    if (std::strcmp(name, "vkCreateDevice") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateDevice);
    if (std::strcmp(name, "vkEnumerateInstanceLayerProperties") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceLayerProperties);

    return g_nextInstanceProcAddr ? g_nextInstanceProcAddr(instance, name) : nullptr;
}
