#include <vulkan/vulkan.h>
#include <iostream>

int main()
{
    uint32_t count = 0;
    VkResult result = vkEnumerateInstanceVersion(&count);
    if (result != VK_SUCCESS) { std::cerr << "vkEnumerateInstanceVersion failed: " << result << "\n"; return 1; }
    std::cout << "Vulkan loader API: " << VK_VERSION_MAJOR(count) << "." << VK_VERSION_MINOR(count) << "." << VK_VERSION_PATCH(count) << "\n";

    VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "SCSKiller Vulkan Probe", VK_MAKE_VERSION(1,0,0), "SCSKiller", VK_MAKE_VERSION(1,0,0), count };
    VkInstanceCreateInfo createInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0, &app, 0, nullptr, 0, nullptr };
    VkInstance instance = VK_NULL_HANDLE;
    result = vkCreateInstance(&createInfo, nullptr, &instance);
    if (result != VK_SUCCESS) { std::cerr << "vkCreateInstance failed: " << result << "\n"; return 1; }

    uint32_t deviceCount = 0;
    result = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (result != VK_SUCCESS) { std::cerr << "vkEnumeratePhysicalDevices failed: " << result << "\n"; vkDestroyInstance(instance, nullptr); return 1; }
    std::cout << "Physical devices: " << deviceCount << "\n";
    vkDestroyInstance(instance, nullptr);
    return 0;
}