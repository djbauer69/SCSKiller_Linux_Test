#include <vulkan/vulkan.h>
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
    const bool graphicsMode = argc >= 2 && std::string(argv[1]) == "--graphics";
    if ((!graphicsMode && argc > 2) || (graphicsMode && argc != 4))
    {
        std::cerr << "Usage: scskiller-vulkan-probe [compute_shader.spv]\n"
                  << "       scskiller-vulkan-probe --graphics vertex.spv fragment.spv\n";
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
        if (!Check(vkCreateRenderPass(
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
                nullptr
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

        VkGraphicsPipelineCreateInfo graphicsInfo{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            nullptr,
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
            renderPass,
            0,
            VK_NULL_HANDLE,
            -1
        };

        VkPipeline graphicsPipeline = VK_NULL_HANDLE;
        if (!Check(vkCreateGraphicsPipelines(
                       device, pipelineCache, 1, &graphicsInfo, nullptr, &graphicsPipeline),
                   "vkCreateGraphicsPipelines"))
        {
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

        std::cout << "Graphics pipeline and render-pass hooks exercised successfully\\n";

        vkDestroyPipeline(device, graphicsPipeline, nullptr);
        vkDestroyRenderPass(device, renderPass, nullptr);
        vkDestroyShaderModule(device, fragmentShader, nullptr);
        vkDestroyShaderModule(device, vertexShader, nullptr);
    }

    if (argc == 2 && !graphicsMode)
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
