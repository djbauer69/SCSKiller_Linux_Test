#include <vulkan/vulkan.h>

#include <cstring>

namespace
{
constexpr char kLayerName[] = "VK_LAYER_SCSKILLER";

VkLayerInstanceCreateInfo* GetLayerCreateInfo(const VkInstanceCreateInfo* createInfo)
{
    auto* current = createInfo
        ? static_cast<VkBaseInStructure*>(const_cast<VkInstanceCreateInfo*>(createInfo)->pNext)
        : nullptr;

    while (current)
    {
        if (current->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO)
        {
            auto* info = reinterpret_cast<VkLayerInstanceCreateInfo*>(current);
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
    auto* linkInfo = GetLayerCreateInfo(createInfo);
    if (!linkInfo || !linkInfo->u.pLayerInfo || !instance)
        return VK_ERROR_INITIALIZATION_FAILED;

    auto next = linkInfo->u.pLayerInfo;
    linkInfo->u.pLayerInfo = next->pNext;

    PFN_vkCreateInstance createNext = reinterpret_cast<PFN_vkCreateInstance>(
        next->pfnNextGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));

    if (!createNext)
        return VK_ERROR_INITIALIZATION_FAILED;

    return createNext(createInfo, allocator, instance);
}

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char* name)
{
    if (!name)
        return nullptr;

    if (std::strcmp(name, "vkCreateInstance") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateInstance);

    if (std::strcmp(name, "vkEnumerateInstanceLayerProperties") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceLayerProperties);

    return nullptr;
}
