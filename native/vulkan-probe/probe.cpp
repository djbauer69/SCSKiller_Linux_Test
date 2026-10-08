#include <vulkan/vulkan.h>
#include <fstream>
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

int main(int argc, char** argv)
{
    if (argc > 2)
    {
        std::cerr << "Usage: scskiller-vulkan-probe [compute_shader.spv]\n";
        return 1;
    }


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

    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    VkPipelineCacheCreateInfo cacheInfo{
        VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        nullptr,
        0,
        0,
        nullptr
    };
    if (!Check(vkCreatePipelineCache(device, &cacheInfo, nullptr, &pipelineCache),
               "vkCreatePipelineCache"))
    {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    if (argc == 2)
    {
        std::ifstream shaderFile(argv[1], std::ios::binary | std::ios::ate);
        if (!shaderFile)
        {
            std::cerr << "Could not open compute shader: " << argv[1] << "\n";
            vkDestroyPipelineCache(device, pipelineCache, nullptr);
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
            vkDestroyDevice(device, nullptr);
            vkDestroyInstance(instance, nullptr);
            return 1;
        }

        const std::streamsize byteCount = shaderFile.tellg();
        if (byteCount <= 0 || byteCount % sizeof(uint32_t) != 0)
        {
            std::cerr << "Compute shader size is not a positive SPIR-V word multiple\n";
            vkDestroyPipelineCache(device, pipelineCache, nullptr);
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
            vkDestroyDevice(device, nullptr);
            vkDestroyInstance(instance, nullptr);
            return 1;
        }

        shaderFile.seekg(0, std::ios::beg);
        std::vector<uint32_t> shaderCode(static_cast<size_t>(byteCount) / sizeof(uint32_t));
        if (!shaderFile.read(reinterpret_cast<char*>(shaderCode.data()), byteCount))
        {
            std::cerr << "Could not read compute shader\n";
            vkDestroyPipelineCache(device, pipelineCache, nullptr);
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
            vkDestroyDevice(device, nullptr);
            vkDestroyInstance(instance, nullptr);
            return 1;
        }

        VkShaderModuleCreateInfo shaderInfo{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            nullptr,
            0,
            shaderCode.size() * sizeof(uint32_t),
            shaderCode.data()
        };
        VkShaderModule shaderModule = VK_NULL_HANDLE;
        if (!Check(vkCreateShaderModule(device, &shaderInfo, nullptr, &shaderModule),
                   "vkCreateShaderModule"))
        {
            vkDestroyPipelineCache(device, pipelineCache, nullptr);
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
            vkDestroyDevice(device, nullptr);
            vkDestroyInstance(instance, nullptr);
            return 1;
        }

        VkPipelineShaderStageCreateInfo stageInfo{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_COMPUTE_BIT,
            shaderModule,
            "main",
            nullptr
        };
        VkComputePipelineCreateInfo computeInfo{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            nullptr,
            0,
            stageInfo,
            pipelineLayout,
            VK_NULL_HANDLE,
            -1
        };
        VkPipeline pipeline = VK_NULL_HANDLE;
        if (!Check(vkCreateComputePipelines(
                       device, pipelineCache, 1, &computeInfo, nullptr, &pipeline),
                   "vkCreateComputePipelines"))
        {
            vkDestroyShaderModule(device, shaderModule, nullptr);
            vkDestroyPipelineCache(device, pipelineCache, nullptr);
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
            vkDestroyDevice(device, nullptr);
            vkDestroyInstance(instance, nullptr);
            return 1;
        }

        std::cout << "Compute pipeline hook exercised successfully\n";
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyShaderModule(device, shaderModule, nullptr);
    }

    size_t cacheSize = 0;
    if (!Check(vkGetPipelineCacheData(device, pipelineCache, &cacheSize, nullptr),
               "vkGetPipelineCacheData(size)"))
    {
        vkDestroyPipelineCache(device, pipelineCache, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    if (cacheSize > 0)
    {
        std::vector<uint8_t> cacheData(cacheSize);
        if (!Check(vkGetPipelineCacheData(device, pipelineCache, &cacheSize, cacheData.data()),
                   "vkGetPipelineCacheData(data)"))
        {
            vkDestroyPipelineCache(device, pipelineCache, nullptr);
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
            vkDestroyDevice(device, nullptr);
            vkDestroyInstance(instance, nullptr);
            return 1;
        }
    }

    std::cout << "Logical device, pipeline layout, and pipeline-cache hooks exercised successfully\n";

    vkDestroyPipelineCache(device, pipelineCache, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return 0;
}
