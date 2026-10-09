#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
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
    const bool graphicsSpecializationMode = argc >= 2 &&
        std::string(argv[1]) == "--graphics-specialization";
    const bool graphicsMode = argc >= 2 &&
        (std::string(argv[1]) == "--graphics" ||
         std::string(argv[1]) == "--dynamic-graphics" ||
         graphicsSpecializationMode);
    const bool dynamicGraphicsMode = argc >= 2 &&
        std::string(argv[1]) == "--dynamic-graphics";
    const bool deviceGroupMode = argc >= 2 &&
        std::string(argv[1]) == "--device-groups";
    const bool specializationMode = argc >= 2 &&
        std::string(argv[1]) == "--specialization";
    const bool feedbackMode = argc >= 2 &&
        std::string(argv[1]) == "--feedback";
    if ((!graphicsMode && !deviceGroupMode && !specializationMode && !feedbackMode && argc > 2) ||
        (graphicsMode && argc != 4) ||
        (deviceGroupMode && argc != 2) ||
        ((specializationMode || feedbackMode) && argc != 3))
    {
        std::cerr << "Usage: scskiller-vulkan-probe [compute_shader.spv]\n"
                  << "       scskiller-vulkan-probe --graphics vertex.spv fragment.spv\n"
                  << "       scskiller-vulkan-probe --dynamic-graphics vertex.spv fragment.spv\n"
                  << "       scskiller-vulkan-probe --graphics-specialization vertex.spv fragment-specialization.spv\n"
                  << "       scskiller-vulkan-probe --device-groups\n"
                  << "       scskiller-vulkan-probe --specialization compute.spv\n"
                  << "       scskiller-vulkan-probe --feedback compute.spv\n";
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
    std::vector<VkPhysicalDevice> physicalDevices;

    if (deviceGroupMode)
    {
        auto enumerateGroups = reinterpret_cast<PFN_vkEnumeratePhysicalDeviceGroups>(
            vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDeviceGroups"));
        if (!enumerateGroups)
        {
            std::cerr << "vkEnumeratePhysicalDeviceGroups is unavailable\n";
            vkDestroyInstance(instance, nullptr);
            return 1;
        }

        uint32_t groupCount = 0;
        if (!Check(enumerateGroups(instance, &groupCount, nullptr),
                   "vkEnumeratePhysicalDeviceGroups(count)"))
        {
            vkDestroyInstance(instance, nullptr);
            return 1;
        }

        std::vector<VkPhysicalDeviceGroupProperties> groups(groupCount);
        for (auto& group : groups)
            group.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES;

        if (groupCount > 0 &&
            !Check(enumerateGroups(instance, &groupCount, groups.data()),
                   "vkEnumeratePhysicalDeviceGroups"))
        {
            vkDestroyInstance(instance, nullptr);
            return 1;
        }

        for (const auto& group : groups)
        {
            for (uint32_t i = 0; i < group.physicalDeviceCount; ++i)
                physicalDevices.push_back(group.physicalDevices[i]);
        }

        deviceCount = static_cast<uint32_t>(physicalDevices.size());
    }
    else
    {
        if (!Check(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr),
                   "vkEnumeratePhysicalDevices(count)"))
        {
            vkDestroyInstance(instance, nullptr);
            return 1;
        }

        physicalDevices.resize(deviceCount);
        if (deviceCount > 0 &&
            !Check(vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data()),
                   "vkEnumeratePhysicalDevices"))
        {
            vkDestroyInstance(instance, nullptr);
            return 1;
        }
    }

    std::cout << "Physical devices: " << deviceCount
              << (deviceGroupMode ? " (from device groups)\n" : "\n");
    if (deviceCount == 0)
    {
        vkDestroyInstance(instance, nullptr);
        return 0;
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

    VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
        nullptr,
        VK_FALSE
    };

    if (dynamicGraphicsMode)
    {
        VkPhysicalDeviceFeatures2 features2{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            &dynamicRenderingFeatures,
            {}
        };
        auto getFeatures2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2"));
        if (!getFeatures2)
        {
            std::cerr << "Dynamic rendering requires vkGetPhysicalDeviceFeatures2\n";
            vkDestroyInstance(instance, nullptr);
            return 1;
        }
        getFeatures2(physicalDevice, &features2);
        if (dynamicRenderingFeatures.dynamicRendering != VK_TRUE)
        {
            std::cerr << "Selected Vulkan device does not support dynamic rendering\n";
            vkDestroyInstance(instance, nullptr);
            return 1;
        }
        dynamicRenderingFeatures.dynamicRendering = VK_TRUE;
        deviceInfo.pNext = &dynamicRenderingFeatures;
    }

    const char* pipelineFeedbackExtension = "VK_EXT_pipeline_creation_feedback";
    if (graphicsMode || feedbackMode)
    {
        uint32_t extensionCount = 0;
        if (!Check(vkEnumerateDeviceExtensionProperties(
                       physicalDevice, nullptr, &extensionCount, nullptr),
                   "vkEnumerateDeviceExtensionProperties(count)"))
        {
            vkDestroyInstance(instance, nullptr);
            return 1;
        }

        std::vector<VkExtensionProperties> extensions(extensionCount);
        if (extensionCount > 0 &&
            !Check(vkEnumerateDeviceExtensionProperties(
                       physicalDevice, nullptr, &extensionCount, extensions.data()),
                   "vkEnumerateDeviceExtensionProperties"))
        {
            vkDestroyInstance(instance, nullptr);
            return 1;
        }

        const bool hasPipelineFeedback = std::any_of(
            extensions.begin(), extensions.end(), [pipelineFeedbackExtension](const auto& extension) {
                return std::strcmp(extension.extensionName, pipelineFeedbackExtension) == 0;
            });
        if (!hasPipelineFeedback)
        {
            std::cerr << "Selected Vulkan device does not support VK_EXT_pipeline_creation_feedback\n";
            vkDestroyInstance(instance, nullptr);
            return 1;
        }

        deviceInfo.enabledExtensionCount = 1;
        deviceInfo.ppEnabledExtensionNames = &pipelineFeedbackExtension;
    }

    VkDevice device = VK_NULL_HANDLE;
    if (!Check(vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device), "vkCreateDevice"))
    {
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    // The ray-tracing extension is deliberately not enabled in this probe.
    // vkGetDeviceProcAddr must not expose its entry point in that state.
    if (vkGetDeviceProcAddr(device, "vkCreateRayTracingPipelinesKHR") != nullptr)
    {
        std::cerr << "Ray-tracing entry point was exposed without enabling its extension\n";
        vkDestroyDevice(device, nullptr);
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

    // Optional: emulate a game that creates its cache from its own persisted
    // data. When SCSKILLER_VK_REPLAY_CACHE is also set, the layer must preserve
    // this initial cache and merge the warmed cache into it.
    std::vector<uint8_t> applicationCacheData;
    if (const char* applicationCachePath = std::getenv("SCSKILLER_PROBE_APP_CACHE"))
    {
        std::ifstream cacheFile(applicationCachePath, std::ios::binary | std::ios::ate);
        if (!cacheFile)
        {
            std::cerr << "Could not read probe application cache: " << applicationCachePath << "\\n";
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
            vkDestroyDevice(device, nullptr);
            vkDestroyInstance(instance, nullptr);
            return 1;
        }

        const auto byteCount = cacheFile.tellg();
        if (byteCount <= 0)
        {
            std::cerr << "Probe application cache is empty\\n";
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
            vkDestroyDevice(device, nullptr);
            vkDestroyInstance(instance, nullptr);
            return 1;
        }

        applicationCacheData.resize(static_cast<size_t>(byteCount));
        cacheFile.seekg(0, std::ios::beg);
        if (!cacheFile.read(reinterpret_cast<char*>(applicationCacheData.data()), byteCount))
        {
            std::cerr << "Could not read all bytes from probe application cache\\n";
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
            vkDestroyDevice(device, nullptr);
            vkDestroyInstance(instance, nullptr);
            return 1;
        }
    }

    VkPipelineCacheCreateInfo cacheInfo{
        VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        nullptr,
        0,
        applicationCacheData.size(),
        applicationCacheData.empty() ? nullptr : applicationCacheData.data()
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

    if (graphicsMode)
    {
        auto readSpirv = [](const char* path, std::vector<uint32_t>& output) -> bool
        {
            std::ifstream shaderFile(path, std::ios::binary | std::ios::ate);
            if (!shaderFile)
                return false;

            const std::streamsize byteCount = shaderFile.tellg();
            if (byteCount <= 0 || byteCount % sizeof(uint32_t) != 0)
                return false;

            shaderFile.seekg(0, std::ios::beg);
            output.resize(static_cast<size_t>(byteCount) / sizeof(uint32_t));
            return shaderFile.read(
                reinterpret_cast<char*>(output.data()), byteCount).good();
        };

        std::vector<uint32_t> vertexCode;
        std::vector<uint32_t> fragmentCode;
        if (!readSpirv(argv[2], vertexCode) || !readSpirv(argv[3], fragmentCode))
        {
            std::cerr << "Could not read graphics shader SPIR-V\\n";
            vkDestroyPipelineCache(device, pipelineCache, nullptr);
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
            vkDestroyDevice(device, nullptr);
            vkDestroyInstance(instance, nullptr);
            return 1;
        }

        VkShaderModuleCreateInfo vertexShaderInfo{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            nullptr,
            0,
            vertexCode.size() * sizeof(uint32_t),
            vertexCode.data()
        };
        VkShaderModuleCreateInfo fragmentShaderInfo{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            nullptr,
            0,
            fragmentCode.size() * sizeof(uint32_t),
            fragmentCode.data()
        };

        VkShaderModule vertexShader = VK_NULL_HANDLE;
        VkShaderModule fragmentShader = VK_NULL_HANDLE;
        if (!Check(vkCreateShaderModule(
                       device, &vertexShaderInfo, nullptr, &vertexShader),
                   "vkCreateShaderModule(vertex)") ||
            !Check(vkCreateShaderModule(
                       device, &fragmentShaderInfo, nullptr, &fragmentShader),
                   "vkCreateShaderModule(fragment)"))
        {
            if (vertexShader)
                vkDestroyShaderModule(device, vertexShader, nullptr);
            vkDestroyShaderModule(device, fragmentShader, nullptr);
            vkDestroyPipelineCache(device, pipelineCache, nullptr);
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
            vkDestroyDevice(device, nullptr);
            vkDestroyInstance(instance, nullptr);
            return 1;
        }

        VkAttachmentDescription colorAttachment{
            0,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_SAMPLE_COUNT_1_BIT,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            VK_ATTACHMENT_STORE_OP_STORE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };
        VkAttachmentReference colorReference{
            0,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };
        VkSubpassDescription subpass{
            0,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            0,
            nullptr,
            1,
            &colorReference,
            nullptr,
            nullptr,
            0,
            nullptr
        };
        VkRenderPassCreateInfo renderPassInfo{
            VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            nullptr,
            0,
            1,
            &colorAttachment,
            1,
            &subpass,
            0,
            nullptr
        };

        VkRenderPass renderPass = VK_NULL_HANDLE;
        if (!dynamicGraphicsMode &&
            !Check(vkCreateRenderPass(
                       device, &renderPassInfo, nullptr, &renderPass),
                   "vkCreateRenderPass"))
        {
            vkDestroyShaderModule(device, fragmentShader, nullptr);
            vkDestroyShaderModule(device, vertexShader, nullptr);
            vkDestroyPipelineCache(device, pipelineCache, nullptr);
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
            vkDestroyDevice(device, nullptr);
            vkDestroyInstance(instance, nullptr);
            return 1;
        }

        uint32_t graphicsSpecializationValue = 7;
        VkSpecializationMapEntry graphicsSpecializationEntry{
            0,
            0,
            sizeof(graphicsSpecializationValue)
        };
        VkSpecializationInfo graphicsSpecializationInfo{
            1,
            &graphicsSpecializationEntry,
            sizeof(graphicsSpecializationValue),
            &graphicsSpecializationValue
        };

        VkPipelineShaderStageCreateInfo stages[2]{
            {
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                nullptr,
                0,
                VK_SHADER_STAGE_VERTEX_BIT,
                vertexShader,
                "main",
                nullptr
            },
            {
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                nullptr,
                0,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                fragmentShader,
                "main",
                graphicsSpecializationMode ? &graphicsSpecializationInfo : nullptr
            }
        };

        VkPipelineVertexInputStateCreateInfo vertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            nullptr,
            0,
            0,
            nullptr,
            0,
            nullptr
        };
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            nullptr,
            0,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            VK_FALSE
        };
        VkViewport viewport{0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, {1, 1}};
        VkPipelineViewportStateCreateInfo viewportState{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            nullptr,
            0,
            1,
            &viewport,
            1,
            &scissor
        };
        VkPipelineRasterizationStateCreateInfo rasterization{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            nullptr,
            0,
            VK_FALSE,
            VK_FALSE,
            VK_POLYGON_MODE_FILL,
            VK_CULL_MODE_NONE,
            VK_FRONT_FACE_COUNTER_CLOCKWISE,
            VK_FALSE,
            0.0f,
            0.0f,
            0.0f,
            1.0f
        };
        VkPipelineMultisampleStateCreateInfo multisample{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            nullptr,
            0,
            VK_SAMPLE_COUNT_1_BIT,
            VK_FALSE,
            1.0f,
            nullptr,
            VK_FALSE,
            VK_FALSE
        };
        VkPipelineColorBlendAttachmentState blendAttachment{
            VK_FALSE,
            VK_BLEND_FACTOR_ONE,
            VK_BLEND_FACTOR_ZERO,
            VK_BLEND_OP_ADD,
            VK_BLEND_FACTOR_ONE,
            VK_BLEND_FACTOR_ZERO,
            VK_BLEND_OP_ADD,
            VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT
        };
        VkPipelineColorBlendStateCreateInfo colorBlend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            nullptr,
            0,
            VK_FALSE,
            VK_LOGIC_OP_COPY,
            1,
            &blendAttachment,
            {0.0f, 0.0f, 0.0f, 0.0f}
        };

        VkPipelineCreationFeedback pipelineFeedback{};
        VkPipelineCreationFeedback stageFeedback[2]{};
        VkPipelineCreationFeedbackCreateInfo feedbackInfo{
            VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO,
            nullptr,
            &pipelineFeedback,
            2,
            stageFeedback
        };

        VkPipelineRenderingCreateInfo renderingInfo{
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            dynamicGraphicsMode ? &feedbackInfo : nullptr,
            0,
            1,
            &colorAttachment.format,
            VK_FORMAT_UNDEFINED,
            VK_FORMAT_UNDEFINED
        };

        VkGraphicsPipelineCreateInfo graphicsInfo{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            dynamicGraphicsMode
                ? static_cast<const void*>(&renderingInfo)
                : static_cast<const void*>(&feedbackInfo),
            0,
            2,
            stages,
            &vertexInput,
            &inputAssembly,
            nullptr,
            &viewportState,
            &rasterization,
            &multisample,
            nullptr,
            &colorBlend,
            nullptr,
            pipelineLayout,
            dynamicGraphicsMode ? VK_NULL_HANDLE : renderPass,
            0,
            VK_NULL_HANDLE,
            -1
        };

        VkPipeline graphicsPipeline = VK_NULL_HANDLE;
        if (!Check(vkCreateGraphicsPipelines(
                       device, pipelineCache, 1, &graphicsInfo, nullptr, &graphicsPipeline),
                   "vkCreateGraphicsPipelines"))
        {
            if (renderPass)
                vkDestroyRenderPass(device, renderPass, nullptr);
            vkDestroyShaderModule(device, fragmentShader, nullptr);
            vkDestroyShaderModule(device, vertexShader, nullptr);
            vkDestroyPipelineCache(device, pipelineCache, nullptr);
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
            vkDestroyDevice(device, nullptr);
            vkDestroyInstance(instance, nullptr);
            return 1;
        }

        std::cout << (graphicsSpecializationMode
            ? "Graphics pipeline with specialization constants exercised successfully\\n"
            : "Graphics pipeline and render-pass hooks exercised successfully\\n");

        vkDestroyPipeline(device, graphicsPipeline, nullptr);
        if (renderPass)
            vkDestroyRenderPass(device, renderPass, nullptr);
        vkDestroyShaderModule(device, fragmentShader, nullptr);
        vkDestroyShaderModule(device, vertexShader, nullptr);
    }

    if (specializationMode || feedbackMode ||
        (argc == 2 && !graphicsMode && !deviceGroupMode))
    {
        const char* computeShaderPath =
            (specializationMode || feedbackMode) ? argv[2] : argv[1];
        std::ifstream shaderFile(computeShaderPath, std::ios::binary | std::ios::ate);
        if (!shaderFile)
        {
            std::cerr << "Could not open compute shader: " << computeShaderPath << "\n";
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

        uint32_t specializationValue = 7;
        VkSpecializationMapEntry specializationEntry{
            0,
            0,
            sizeof(specializationValue)
        };
        VkSpecializationInfo specializationInfo{
            1,
            &specializationEntry,
            sizeof(specializationValue),
            &specializationValue
        };

        VkPipelineShaderStageCreateInfo stageInfo{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_COMPUTE_BIT,
            shaderModule,
            "main",
            specializationMode ? &specializationInfo : nullptr
        };
        VkPipelineCreationFeedback pipelineFeedback{};
        VkPipelineCreationFeedback stageFeedback{};
        VkPipelineCreationFeedbackCreateInfo feedbackInfo{
            VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO,
            nullptr,
            &pipelineFeedback,
            1,
            &stageFeedback
        };

        VkComputePipelineCreateInfo computeInfo{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            feedbackMode ? &feedbackInfo : nullptr,
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

        std::cout << (specializationMode
            ? "Compute pipeline with specialization constants exercised successfully\n"
            : feedbackMode
                ? "Compute pipeline with pipeline-creation feedback exercised successfully\n"
                : "Compute pipeline hook exercised successfully\n");
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
