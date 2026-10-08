#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>

namespace
{
bool Check(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
    {
        std::cerr << operation << " failed: " << result << "\n";
        return false;
    }
    return true;
}
}

int main()
{
    uint32_t apiVersion = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion(&apiVersion) != VK_SUCCESS)
    {
        std::cerr << "vkEnumerateInstanceVersion failed\n";
        return 1;
    }

    std::cout << "Vulkan loader API: "
              << VK_VERSION_MAJOR(apiVersion) << "."
              << VK_VERSION_MINOR(apiVersion) << "."
              << VK_VERSION_PATCH(apiVersion) << "\n";

    VkApplicationInfo app{
        VK_STRUCTURE_TYPE_APPLICATION_INFO,
        nullptr,
        "SCSKiller Vulkan Probe",
        VK_MAKE_VERSION(1, 0, 0),
        "SCSKiller",
        VK_MAKE_VERSION(1, 0, 0),
        apiVersion
    };

    VkInstanceCreateInfo createInfo{
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        nullptr,
        0,
        &app,
        0,
        nullptr,
        0,
        nullptr
    };

    VkInstance instance = VK_NULL_HANDLE;
    if (!Check(vkCreateInstance(&createInfo, nullptr, &instance), "vkCreateInstance"))
        return 1;

    uint32_t deviceCount = 0;
    if (!Check(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr),
               "vkEnumeratePhysicalDevices(count)"))
    {
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    std::cout << "Physical devices: " << deviceCount << "\n";
    if (deviceCount == 0)
    {
        vkDestroyInstance(instance, nullptr);
        return 0;
    }

    std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
    if (!Check(vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data()),
               "vkEnumeratePhysicalDevices"))
    {
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    VkPhysicalDevice physicalDevice = physicalDevices[0];
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(
        physicalDevice, &queueFamilyCount, queueFamilies.data());

    uint32_t queueFamily = VK_QUEUE_FAMILY_IGNORED;
    for (uint32_t i = 0; i < queueFamilyCount; ++i)
    {
        if (queueFamilies[i].queueCount > 0 &&
            (queueFamilies[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)))
        {
            queueFamily = i;
            break;
        }
    }

    if (queueFamily == VK_QUEUE_FAMILY_IGNORED)
    {
        std::cerr << "No graphics/compute queue family found\n";
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        nullptr,
        0,
        queueFamily,
        1,
        &queuePriority
    };

    VkDeviceCreateInfo deviceInfo{
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        nullptr,
        0,
        1,
        &queueInfo,
        0,
        nullptr,
        0,
        nullptr,
        0,
        nullptr
    };

    VkDevice device = VK_NULL_HANDLE;
    if (!Check(vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device), "vkCreateDevice"))
    {
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    VkDescriptorSetLayoutCreateInfo descriptorInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        nullptr,
        0,
        0,
        nullptr
    };

    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    if (!Check(vkCreateDescriptorSetLayout(device, &descriptorInfo, nullptr, &descriptorLayout),
               "vkCreateDescriptorSetLayout"))
    {
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    VkPipelineLayoutCreateInfo pipelineInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        nullptr,
        0,
        1,
        &descriptorLayout,
        0,
        nullptr
    };

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (!Check(vkCreatePipelineLayout(device, &pipelineInfo, nullptr, &pipelineLayout),
               "vkCreatePipelineLayout"))
    {
        vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    std::cout << "Logical device and pipeline-layout hooks exercised successfully\n";

    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return 0;
}
