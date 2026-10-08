#include <vulkan/vulkan.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace
{
struct ShaderRecord
{
    std::vector<uint32_t> words;
};

struct DescriptorBindingRecord
{
    uint32_t binding = 0;
    VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    uint32_t descriptorCount = 0;
    VkShaderStageFlags stageFlags = 0;
    uint32_t immutableSamplerCount = 0;
};

struct DescriptorLayoutRecord
{
    VkDescriptorSetLayoutCreateFlags flags = 0;
    std::vector<DescriptorBindingRecord> bindings;
};

struct PushConstantRecord
{
    VkShaderStageFlags stageFlags = 0;
    uint32_t offset = 0;
    uint32_t size = 0;
};

struct RenderPassAttachmentRecord
{
    VkAttachmentDescription description{};
};

struct RenderPassReferenceRecord
{
    uint32_t attachment = VK_ATTACHMENT_UNUSED;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct RenderPassSubpassRecord
{
    VkSubpassDescriptionFlags flags = 0;
    VkPipelineBindPoint pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    std::vector<RenderPassReferenceRecord> inputAttachments;
    std::vector<RenderPassReferenceRecord> colorAttachments;
    std::vector<RenderPassReferenceRecord> resolveAttachments;
    bool hasDepthStencil = false;
    RenderPassReferenceRecord depthStencil;
    std::vector<uint32_t> preserveAttachments;
};

struct RenderPassDependencyRecord
{
    uint32_t srcSubpass = VK_SUBPASS_EXTERNAL;
    uint32_t dstSubpass = VK_SUBPASS_EXTERNAL;
    VkPipelineStageFlags srcStageMask = 0;
    VkPipelineStageFlags dstStageMask = 0;
    VkAccessFlags srcAccessMask = 0;
    VkAccessFlags dstAccessMask = 0;
    VkDependencyFlags dependencyFlags = 0;
};

struct RenderPassRecord
{
    VkRenderPassCreateFlags flags = 0;
    bool replayCompatible = false;
    std::vector<RenderPassAttachmentRecord> attachments;
    std::vector<RenderPassSubpassRecord> subpasses;
    std::vector<RenderPassDependencyRecord> dependencies;
};

struct PipelineLayoutRecord
{
    VkPipelineLayoutCreateFlags flags = 0;
    std::vector<uint64_t> setLayoutHashes;
    std::vector<PushConstantRecord> pushConstants;
};

struct SpecializationRecord
{
    bool present = false;
    std::vector<VkSpecializationMapEntry> mapEntries;
    std::vector<uint8_t> data;
};

struct ComputePipelineRecord
{
    uint64_t layoutHash = 0;
    uint64_t moduleHash = 0;
    VkPipelineCreateFlags flags = 0;
    VkPipelineShaderStageCreateFlags stageFlags = 0;
    std::string entryPoint = "main";
    SpecializationRecord specialization;
};

struct GraphicsStageRecord
{
    VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT;
    VkPipelineShaderStageCreateFlags flags = 0;
    uint64_t moduleHash = 0;
    std::string entryPoint = "main";
    SpecializationRecord specialization;
};

struct ViewportRecord
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

struct ScissorRecord
{
    int32_t offsetX = 0;
    int32_t offsetY = 0;
    uint32_t width = 1;
    uint32_t height = 1;
};

struct VertexBindingRecord
{
    uint32_t binding = 0;
    uint32_t stride = 0;
    VkVertexInputRate inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
};

struct VertexAttributeRecord
{
    uint32_t location = 0;
    uint32_t binding = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t offset = 0;
};

struct StencilRecord
{
    VkStencilOp failOp = VK_STENCIL_OP_KEEP;
    VkStencilOp passOp = VK_STENCIL_OP_KEEP;
    VkStencilOp depthFailOp = VK_STENCIL_OP_KEEP;
    VkCompareOp compareOp = VK_COMPARE_OP_ALWAYS;
    uint32_t compareMask = 0;
    uint32_t writeMask = 0;
    uint32_t reference = 0;
};

struct ColorBlendAttachmentRecord
{
    VkBool32 blendEnable = VK_FALSE;
    VkBlendFactor srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp colorBlendOp = VK_BLEND_OP_ADD;
    VkBlendFactor srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp alphaBlendOp = VK_BLEND_OP_ADD;
    VkColorComponentFlags colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                            VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT |
                                            VK_COLOR_COMPONENT_A_BIT;
};

struct GraphicsPipelineRecord
{
    uint64_t layoutHash = 0;
    uint64_t renderPassHash = 0;
    VkPipelineCreateFlags flags = 0;
    uint32_t subpass = 0;
    int32_t basePipelineIndex = -1;
    std::vector<GraphicsStageRecord> stages;

    bool hasVertexInput = false;
    bool hasInputAssembly = false;
    std::vector<VertexBindingRecord> vertexBindings;
    std::vector<VertexAttributeRecord> vertexAttributes;

    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkBool32 primitiveRestartEnable = VK_FALSE;

    bool hasTessellation = false;
    uint32_t patchControlPoints = 0;

    bool hasViewportState = false;
    uint32_t viewportCount = 1;
    uint32_t scissorCount = 1;
    std::vector<ViewportRecord> viewports;
    std::vector<ScissorRecord> scissors;

    bool hasRasterization = false;
    VkBool32 depthClampEnable = VK_FALSE;
    VkBool32 rasterizerDiscardEnable = VK_FALSE;
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cullMode = VK_CULL_MODE_NONE;
    VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkBool32 depthBiasEnable = VK_FALSE;
    float depthBiasConstantFactor = 0.0f;
    float depthBiasClamp = 0.0f;
    float depthBiasSlopeFactor = 0.0f;
    float lineWidth = 1.0f;

    bool hasMultisample = false;
    VkSampleCountFlagBits rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkBool32 sampleShadingEnable = VK_FALSE;
    float minSampleShading = 1.0f;
    VkBool32 alphaToCoverageEnable = VK_FALSE;
    VkBool32 alphaToOneEnable = VK_FALSE;
    std::vector<uint32_t> sampleMask;

    bool hasDepthStencil = false;
    VkBool32 depthTestEnable = VK_FALSE;
    VkBool32 depthWriteEnable = VK_FALSE;
    VkCompareOp depthCompareOp = VK_COMPARE_OP_ALWAYS;
    VkBool32 depthBoundsTestEnable = VK_FALSE;
    float minDepthBounds = 0.0f;
    float maxDepthBounds = 1.0f;
    VkBool32 stencilTestEnable = VK_FALSE;
    StencilRecord frontStencil;
    StencilRecord backStencil;

    bool hasColorBlend = false;
    VkBool32 logicOpEnable = VK_FALSE;
    VkLogicOp logicOp = VK_LOGIC_OP_COPY;
    float blendConstants[4] = {};
    std::vector<ColorBlendAttachmentRecord> colorBlendAttachments;

    std::vector<VkDynamicState> dynamicStates;

    bool dynamicRendering = false;
    uint32_t viewMask = 0;
    std::vector<VkFormat> colorFormats;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkFormat stencilFormat = VK_FORMAT_UNDEFINED;

    bool replayCompatible = true;
};

struct Recording
{
    std::unordered_map<uint64_t, ShaderRecord> shaders;
    std::unordered_map<uint64_t, DescriptorLayoutRecord> descriptorLayouts;
    std::unordered_map<uint64_t, PipelineLayoutRecord> pipelineLayouts;
    std::unordered_map<uint64_t, RenderPassRecord> renderPasses;
    std::vector<ComputePipelineRecord> computePipelines;
    std::vector<GraphicsPipelineRecord> graphicsPipelinesToReplay;
    size_t graphicsPipelines = 0;
    size_t rayTracingPipelines = 0;
};

struct VkContext
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkPipelineCache cache = VK_NULL_HANDLE;
    bool dynamicRenderingEnabled = false;
};

std::string Trim(std::string value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return value;
}

bool FindToken(std::string_view line, std::string_view key, size_t& valueStart)
{
    const std::string needle = "\"" + std::string(key) + "\":";
    const size_t keyPos = line.find(needle);
    if (keyPos == std::string_view::npos)
        return false;

    valueStart = keyPos + needle.size();
    while (valueStart < line.size() &&
           std::isspace(static_cast<unsigned char>(line[valueStart])))
    {
        ++valueStart;
    }
    return valueStart < line.size();
}

bool FindString(std::string_view line, std::string_view key, std::string& value)
{
    size_t start = 0;
    if (!FindToken(line, key, start) || line[start] != '"')
        return false;

    ++start;
    std::string result;
    bool escaped = false;
    for (size_t i = start; i < line.size(); ++i)
    {
        const char c = line[i];
        if (escaped)
        {
            switch (c)
            {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            default: result.push_back(c); break;
            }
            escaped = false;
            continue;
        }
        if (c == '\\')
        {
            escaped = true;
            continue;
        }
        if (c == '"')
        {
            value = std::move(result);
            return true;
        }
        result.push_back(c);
    }
    return false;
}

bool FindUnsigned(std::string_view line, std::string_view key, uint64_t& value)
{
    size_t start = 0;
    if (!FindToken(line, key, start))
        return false;

    size_t end = start;
    while (end < line.size() && std::isdigit(static_cast<unsigned char>(line[end])))
        ++end;
    if (end == start)
        return false;

    try
    {
        value = std::stoull(std::string(line.substr(start, end - start)));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool FindHex(std::string_view line, std::string_view key, uint64_t& value)
{
    std::string text;
    if (!FindString(line, key, text) || text.empty())
        return false;

    try
    {
        value = std::stoull(text, nullptr, 16);
        return true;
    }
    catch (...)
    {
        return false;
    }
}


bool DecodeBase64(std::string_view encoded, std::vector<uint8_t>& output);
bool ExtractArray(std::string_view line, std::string_view key, std::string_view& contents);
bool ExtractObject(std::string_view line, std::string_view key, std::string_view& contents);
std::vector<std::string_view> SplitArray(std::string_view contents);

bool FindBool(std::string_view line, std::string_view key, bool& value)
{
    size_t start = 0;
    if (!FindToken(line, key, start))
        return false;

    if (line.substr(start, 4) == "true")
    {
        value = true;
        return true;
    }
    if (line.substr(start, 5) == "false")
    {
        value = false;
        return true;
    }
    return false;
}

bool FindSigned(std::string_view line, std::string_view key, int64_t& value)
{
    size_t start = 0;
    if (!FindToken(line, key, start))
        return false;

    size_t end = start;
    if (end < line.size() && line[end] == '-')
        ++end;
    const size_t digitsStart = end;
    while (end < line.size() &&
           std::isdigit(static_cast<unsigned char>(line[end])))
    {
        ++end;
    }
    if (end == digitsStart)
        return false;

    try
    {
        value = std::stoll(std::string(line.substr(start, end - start)));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool FindFloat(std::string_view line, std::string_view key, float& value)
{
    size_t start = 0;
    if (!FindToken(line, key, start))
        return false;

    size_t end = start;
    while (end < line.size() &&
           line[end] != ',' &&
           line[end] != '}' &&
           line[end] != ']')
    {
        ++end;
    }

    try
    {
        value = std::stof(Trim(std::string(line.substr(start, end - start))));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool ParseSpecialization(
    std::string_view object,
    SpecializationRecord& specialization,
    std::string& error)
{
    specialization.present = true;

    std::string encoded;
    if (FindString(object, "data_base64", encoded) &&
        !encoded.empty() &&
        !DecodeBase64(encoded, specialization.data))
    {
        error = "invalid specialization Base64 payload";
        return false;
    }

    std::string_view mapEntries;
    if (!ExtractArray(object, "map_entries", mapEntries))
    {
        error = "specialization is missing map_entries";
        return false;
    }

    for (const auto mapText : SplitArray(mapEntries))
    {
        const std::string mapEntry = Trim(std::string(mapText));
        if (mapEntry.empty())
            continue;

        VkSpecializationMapEntry entry{};
        uint64_t value = 0;
        if (!FindUnsigned(mapEntry, "constant_id", value))
            return false;
        entry.constantID = static_cast<uint32_t>(value);
        if (!FindUnsigned(mapEntry, "offset", value))
            return false;
        entry.offset = static_cast<size_t>(value);
        if (!FindUnsigned(mapEntry, "size", value))
            return false;
        entry.size = static_cast<size_t>(value);
        specialization.mapEntries.push_back(entry);
    }

    return true;
}

bool ParseShaderStage(
    std::string_view object,
    GraphicsStageRecord& stage,
    std::string& error)
{
    std::string stageName;
    if (!FindString(object, "stage", stageName))
        return false;

    if (stageName == "vertex") stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    else if (stageName == "tessellation_control") stage.stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    else if (stageName == "tessellation_evaluation") stage.stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    else if (stageName == "geometry") stage.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
    else if (stageName == "fragment") stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    else if (stageName == "compute") stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    else if (stageName == "task") stage.stage = VK_SHADER_STAGE_TASK_BIT_EXT;
    else if (stageName == "mesh") stage.stage = VK_SHADER_STAGE_MESH_BIT_EXT;
    else
    {
        error = "unsupported graphics shader stage";
        return false;
    }

    uint64_t value = 0;
    if (!FindUnsigned(object, "stage_flags", value))
        return false;
    stage.flags = static_cast<VkPipelineShaderStageCreateFlags>(value);
    if (!FindHex(object, "module_hash", stage.moduleHash))
        return false;
    FindString(object, "entry_point", stage.entryPoint);

    std::string_view specialization;
    if (ExtractObject(object, "specialization", specialization))
    {
        if (!ParseSpecialization(specialization, stage.specialization, error))
            return false;
    }

    return true;
}

bool ExtractArray(std::string_view line, std::string_view key, std::string_view& contents)
{
    size_t start = 0;
    if (!FindToken(line, key, start) || line[start] != '[')
        return false;

    const size_t contentStart = start + 1;
    int depth = 1;
    bool quoted = false;
    bool escaped = false;
    for (size_t i = contentStart; i < line.size(); ++i)
    {
        const char c = line[i];
        if (quoted)
        {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
            continue;
        }

        if (c == '"')
        {
            quoted = true;
            continue;
        }
        if (c == '[')
            ++depth;
        else if (c == ']')
        {
            --depth;
            if (depth == 0)
            {
                contents = line.substr(contentStart, i - contentStart);
                return true;
            }
        }
    }
    return false;
}

bool ExtractObject(std::string_view line, std::string_view key, std::string_view& contents)
{
    size_t start = 0;
    if (!FindToken(line, key, start) || line[start] != '{')
        return false;

    const size_t contentStart = start + 1;
    int depth = 1;
    bool quoted = false;
    bool escaped = false;
    for (size_t i = contentStart; i < line.size(); ++i)
    {
        const char c = line[i];
        if (quoted)
        {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
            continue;
        }

        if (c == '"')
            quoted = true;
        else if (c == '{')
            ++depth;
        else if (c == '}')
        {
            --depth;
            if (depth == 0)
            {
                contents = line.substr(contentStart, i - contentStart);
                return true;
            }
        }
    }
    return false;
}

std::vector<std::string_view> SplitArray(std::string_view contents)
{
    std::vector<std::string_view> parts;
    size_t start = 0;
    int objectDepth = 0;
    int arrayDepth = 0;
    bool quoted = false;
    bool escaped = false;

    for (size_t i = 0; i < contents.size(); ++i)
    {
        const char c = contents[i];
        if (quoted)
        {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
            continue;
        }

        if (c == '"')
            quoted = true;
        else if (c == '{')
            ++objectDepth;
        else if (c == '}')
            --objectDepth;
        else if (c == '[')
            ++arrayDepth;
        else if (c == ']')
            --arrayDepth;
        else if (c == ',' && objectDepth == 0 && arrayDepth == 0)
        {
            parts.push_back(contents.substr(start, i - start));
            start = i + 1;
        }
    }

    if (start < contents.size())
        parts.push_back(contents.substr(start));

    return parts;
}

bool DecodeBase64(std::string_view encoded, std::vector<uint8_t>& output)
{
    auto value = [](char c) -> int
    {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };

    uint32_t accumulator = 0;
    int bits = 0;
    output.clear();

    for (const char c : encoded)
    {
        if (c == '=')
            break;

        const int decoded = value(c);
        if (decoded < 0)
            continue;

        accumulator = (accumulator << 6) | static_cast<uint32_t>(decoded);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            output.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xff));
        }
    }

    return !encoded.empty() && !output.empty();
}


bool ParseGraphicsPipelineState(
    std::string_view line,
    Recording& recording,
    std::string& error)
{
    std::string_view pipelines;
    if (!ExtractArray(line, "pipelines", pipelines))
    {
        error = "graphics_pipeline_state is missing pipelines";
        return false;
    }

    for (const auto pipelineText : SplitArray(pipelines))
    {
        const std::string entry = Trim(std::string(pipelineText));
        if (entry.empty())
            continue;

        GraphicsPipelineRecord record;
        uint64_t value = 0;

        if (!FindHex(entry, "layout_hash", record.layoutHash))
            return false;
        if (!FindHex(entry, "render_pass_hash", record.renderPassHash))
            return false;
        if (!FindUnsigned(entry, "flags", value))
            return false;
        record.flags = static_cast<VkPipelineCreateFlags>(value);
        if (!FindUnsigned(entry, "subpass", value))
            return false;
        record.subpass = static_cast<uint32_t>(value);

        int64_t signedValue = -1;
        if (FindSigned(entry, "base_pipeline_index", signedValue))
            record.basePipelineIndex = static_cast<int32_t>(signedValue);

        bool compatible = true;
        if (FindBool(entry, "replay_compatible", compatible))
            record.replayCompatible = compatible;

        bool legacyRenderPass = false;
        if (FindBool(entry, "legacy_render_pass", legacyRenderPass) &&
            !legacyRenderPass)
        {
            record.replayCompatible = false;
        }

        std::string_view dynamicRendering;
        if (ExtractObject(entry, "dynamic_rendering", dynamicRendering))
        {
            record.dynamicRendering = true;

            if (!FindUnsigned(dynamicRendering, "view_mask", value))
                return false;
            record.viewMask = static_cast<uint32_t>(value);
            if (!FindUnsigned(dynamicRendering, "depth_format", value))
                return false;
            record.depthFormat = static_cast<VkFormat>(value);
            if (!FindUnsigned(dynamicRendering, "stencil_format", value))
                return false;
            record.stencilFormat = static_cast<VkFormat>(value);

            std::string_view formats;
            if (!ExtractArray(dynamicRendering, "color_formats", formats))
                return false;
            for (const auto formatText : SplitArray(formats))
            {
                const std::string formatEntry = Trim(std::string(formatText));
                if (formatEntry.empty())
                    continue;
                if (!FindSigned(formatEntry, "value", signedValue))
                {
                    try
                    {
                        record.colorFormats.push_back(
                            static_cast<VkFormat>(std::stol(formatEntry)));
                    }
                    catch (...)
                    {
                        return false;
                    }
                }
            }
        }

        std::string_view stages;
        if (!ExtractArray(entry, "stages", stages))
            return false;
        for (const auto stageText : SplitArray(stages))
        {
            const std::string stageEntry = Trim(std::string(stageText));
            if (stageEntry.empty())
                continue;

            GraphicsStageRecord stage;
            if (!ParseShaderStage(stageEntry, stage, error))
                return false;

            bool stagePnext = false;
            if (FindBool(stageEntry, "pnext_present", stagePnext) && stagePnext)
                record.replayCompatible = false;

            record.stages.push_back(std::move(stage));
        }

        std::string_view vertexInput;
        if (ExtractObject(entry, "vertex_input", vertexInput))
        {
            record.hasVertexInput = true;
            uint64_t flags = 0;
            FindUnsigned(vertexInput, "flags", flags);

            std::string_view bindings;
            if (!ExtractArray(vertexInput, "bindings", bindings))
                return false;
            for (const auto bindingText : SplitArray(bindings))
            {
                const std::string bindingEntry = Trim(std::string(bindingText));
                if (bindingEntry.empty())
                    continue;
                VertexBindingRecord binding;
                if (!FindUnsigned(bindingEntry, "binding", value)) return false;
                binding.binding = static_cast<uint32_t>(value);
                if (!FindUnsigned(bindingEntry, "stride", value)) return false;
                binding.stride = static_cast<uint32_t>(value);
                if (!FindUnsigned(bindingEntry, "input_rate", value)) return false;
                binding.inputRate = static_cast<VkVertexInputRate>(value);
                record.vertexBindings.push_back(binding);
            }

            std::string_view attributes;
            if (!ExtractArray(vertexInput, "attributes", attributes))
                return false;
            for (const auto attributeText : SplitArray(attributes))
            {
                const std::string attributeEntry = Trim(std::string(attributeText));
                if (attributeEntry.empty())
                    continue;
                VertexAttributeRecord attribute;
                if (!FindUnsigned(attributeEntry, "location", value)) return false;
                attribute.location = static_cast<uint32_t>(value);
                if (!FindUnsigned(attributeEntry, "binding", value)) return false;
                attribute.binding = static_cast<uint32_t>(value);
                if (!FindUnsigned(attributeEntry, "format", value)) return false;
                attribute.format = static_cast<VkFormat>(value);
                if (!FindUnsigned(attributeEntry, "offset", value)) return false;
                attribute.offset = static_cast<uint32_t>(value);
                record.vertexAttributes.push_back(attribute);
            }

            bool pnext = false;
            if (FindBool(vertexInput, "pnext_present", pnext) && pnext)
                record.replayCompatible = false;
        }
        else if (entry.find("\"vertex_input\":null") == std::string::npos)
        {
            return false;
        }

        std::string_view inputAssembly;
        if (ExtractObject(entry, "input_assembly", inputAssembly))
        {
            record.hasInputAssembly = true;
            if (!FindUnsigned(inputAssembly, "topology", value)) return false;
            record.topology = static_cast<VkPrimitiveTopology>(value);
            bool primitiveRestart = false;
            if (FindBool(inputAssembly, "primitive_restart", primitiveRestart))
                record.primitiveRestartEnable = primitiveRestart ? VK_TRUE : VK_FALSE;

            bool pnext = false;
            if (FindBool(inputAssembly, "pnext_present", pnext) && pnext)
                record.replayCompatible = false;
        }

        std::string_view tessellation;
        if (ExtractObject(entry, "tessellation", tessellation))
        {
            record.hasTessellation = true;
            if (!FindUnsigned(tessellation, "patch_control_points", value)) return false;
            record.patchControlPoints = static_cast<uint32_t>(value);

            bool pnext = false;
            if (FindBool(tessellation, "pnext_present", pnext) && pnext)
                record.replayCompatible = false;
        }

        std::string_view viewportState;
        if (ExtractObject(entry, "viewport_state", viewportState))
        {
            record.hasViewportState = true;
            if (!FindUnsigned(viewportState, "viewport_count", value)) return false;
            record.viewportCount = static_cast<uint32_t>(value);
            if (!FindUnsigned(viewportState, "scissor_count", value)) return false;
            record.scissorCount = static_cast<uint32_t>(value);

            std::string_view viewports;
            if (!ExtractArray(viewportState, "viewports", viewports))
                return false;
            for (const auto viewportText : SplitArray(viewports))
            {
                const std::string viewportEntry = Trim(std::string(viewportText));
                if (viewportEntry.empty())
                    continue;
                ViewportRecord viewport;
                if (!FindFloat(viewportEntry, "x", viewport.x)) return false;
                if (!FindFloat(viewportEntry, "y", viewport.y)) return false;
                if (!FindFloat(viewportEntry, "width", viewport.width)) return false;
                if (!FindFloat(viewportEntry, "height", viewport.height)) return false;
                if (!FindFloat(viewportEntry, "min_depth", viewport.minDepth)) return false;
                if (!FindFloat(viewportEntry, "max_depth", viewport.maxDepth)) return false;
                record.viewports.push_back(viewport);
            }

            std::string_view scissors;
            if (!ExtractArray(viewportState, "scissors", scissors))
                return false;
            for (const auto scissorText : SplitArray(scissors))
            {
                const std::string scissorEntry = Trim(std::string(scissorText));
                if (scissorEntry.empty())
                    continue;
                ScissorRecord scissor;
                int64_t signedCoordinate = 0;
                if (!FindSigned(scissorEntry, "offset_x", signedCoordinate)) return false;
                scissor.offsetX = static_cast<int32_t>(signedCoordinate);
                if (!FindSigned(scissorEntry, "offset_y", signedCoordinate)) return false;
                scissor.offsetY = static_cast<int32_t>(signedCoordinate);
                if (!FindUnsigned(scissorEntry, "extent_width", value)) return false;
                scissor.width = static_cast<uint32_t>(value);
                if (!FindUnsigned(scissorEntry, "extent_height", value)) return false;
                scissor.height = static_cast<uint32_t>(value);
                record.scissors.push_back(scissor);
            }

            bool pnext = false;
            if (FindBool(viewportState, "pnext_present", pnext) && pnext)
                record.replayCompatible = false;
        }

        std::string_view rasterization;
        if (ExtractObject(entry, "rasterization", rasterization))
        {
            record.hasRasterization = true;
            bool booleanValue = false;
            if (FindBool(rasterization, "depth_clamp", booleanValue))
                record.depthClampEnable = booleanValue ? VK_TRUE : VK_FALSE;
            if (FindBool(rasterization, "rasterizer_discard", booleanValue))
                record.rasterizerDiscardEnable = booleanValue ? VK_TRUE : VK_FALSE;
            if (!FindUnsigned(rasterization, "polygon_mode", value)) return false;
            record.polygonMode = static_cast<VkPolygonMode>(value);
            if (!FindUnsigned(rasterization, "cull_mode", value)) return false;
            record.cullMode = static_cast<VkCullModeFlags>(value);
            if (!FindUnsigned(rasterization, "front_face", value)) return false;
            record.frontFace = static_cast<VkFrontFace>(value);
            if (FindBool(rasterization, "depth_bias_enable", booleanValue))
                record.depthBiasEnable = booleanValue ? VK_TRUE : VK_FALSE;
            if (!FindFloat(rasterization, "depth_bias_constant", record.depthBiasConstantFactor)) return false;
            if (!FindFloat(rasterization, "depth_bias_clamp", record.depthBiasClamp)) return false;
            if (!FindFloat(rasterization, "depth_bias_slope", record.depthBiasSlopeFactor)) return false;
            if (!FindFloat(rasterization, "line_width", record.lineWidth)) return false;

            bool pnext = false;
            if (FindBool(rasterization, "pnext_present", pnext) && pnext)
                record.replayCompatible = false;
        }

        std::string_view multisample;
        if (ExtractObject(entry, "multisample", multisample))
        {
            record.hasMultisample = true;
            if (!FindUnsigned(multisample, "rasterization_samples", value)) return false;
            record.rasterizationSamples = static_cast<VkSampleCountFlagBits>(value);

            bool booleanValue = false;
            if (FindBool(multisample, "sample_shading", booleanValue))
                record.sampleShadingEnable = booleanValue ? VK_TRUE : VK_FALSE;
            if (!FindFloat(multisample, "min_sample_shading", record.minSampleShading)) return false;
            if (FindBool(multisample, "alpha_to_coverage", booleanValue))
                record.alphaToCoverageEnable = booleanValue ? VK_TRUE : VK_FALSE;
            if (FindBool(multisample, "alpha_to_one", booleanValue))
                record.alphaToOneEnable = booleanValue ? VK_TRUE : VK_FALSE;

            std::string_view sampleMask;
            if (!ExtractArray(multisample, "sample_mask", sampleMask))
                return false;
            for (const auto wordText : SplitArray(sampleMask))
            {
                const std::string word = Trim(std::string(wordText));
                if (word.empty()) continue;
                if (!FindUnsigned(word, "value", value))
                {
                    try { record.sampleMask.push_back(static_cast<uint32_t>(std::stoull(word))); }
                    catch (...) { return false; }
                }
            }

            bool pnext = false;
            if (FindBool(multisample, "pnext_present", pnext) && pnext)
                record.replayCompatible = false;
        }

        auto parseStencil = [&](std::string_view object, StencilRecord& stencil) -> bool
        {
            if (!FindUnsigned(object, "fail_op", value)) return false;
            stencil.failOp = static_cast<VkStencilOp>(value);
            if (!FindUnsigned(object, "pass_op", value)) return false;
            stencil.passOp = static_cast<VkStencilOp>(value);
            if (!FindUnsigned(object, "depth_fail_op", value)) return false;
            stencil.depthFailOp = static_cast<VkStencilOp>(value);
            if (!FindUnsigned(object, "compare_op", value)) return false;
            stencil.compareOp = static_cast<VkCompareOp>(value);
            if (!FindUnsigned(object, "compare_mask", value)) return false;
            stencil.compareMask = static_cast<uint32_t>(value);
            if (!FindUnsigned(object, "write_mask", value)) return false;
            stencil.writeMask = static_cast<uint32_t>(value);
            if (!FindUnsigned(object, "reference", value)) return false;
            stencil.reference = static_cast<uint32_t>(value);
            return true;
        };

        std::string_view depthStencil;
        if (ExtractObject(entry, "depth_stencil", depthStencil))
        {
            record.hasDepthStencil = true;
            bool booleanValue = false;
            if (FindBool(depthStencil, "depth_test", booleanValue))
                record.depthTestEnable = booleanValue ? VK_TRUE : VK_FALSE;
            if (FindBool(depthStencil, "depth_write", booleanValue))
                record.depthWriteEnable = booleanValue ? VK_TRUE : VK_FALSE;
            if (!FindUnsigned(depthStencil, "depth_compare_op", value)) return false;
            record.depthCompareOp = static_cast<VkCompareOp>(value);
            if (FindBool(depthStencil, "depth_bounds", booleanValue))
                record.depthBoundsTestEnable = booleanValue ? VK_TRUE : VK_FALSE;
            if (!FindFloat(depthStencil, "min_depth_bounds", record.minDepthBounds)) return false;
            if (!FindFloat(depthStencil, "max_depth_bounds", record.maxDepthBounds)) return false;
            if (FindBool(depthStencil, "stencil_test", booleanValue))
                record.stencilTestEnable = booleanValue ? VK_TRUE : VK_FALSE;

            std::string_view front;
            if (!ExtractObject(depthStencil, "front", front) ||
                !parseStencil(front, record.frontStencil))
                return false;
            std::string_view back;
            if (!ExtractObject(depthStencil, "back", back) ||
                !parseStencil(back, record.backStencil))
                return false;

            bool pnext = false;
            if (FindBool(depthStencil, "pnext_present", pnext) && pnext)
                record.replayCompatible = false;
        }

        std::string_view colorBlend;
        if (ExtractObject(entry, "color_blend", colorBlend))
        {
            record.hasColorBlend = true;
            bool booleanValue = false;
            if (FindBool(colorBlend, "logic_op_enable", booleanValue))
                record.logicOpEnable = booleanValue ? VK_TRUE : VK_FALSE;
            if (!FindUnsigned(colorBlend, "logic_op", value)) return false;
            record.logicOp = static_cast<VkLogicOp>(value);

            std::string_view constants;
            if (!ExtractArray(colorBlend, "blend_constants", constants))
                return false;
            for (const auto constantText : SplitArray(constants))
            {
                const std::string constant = Trim(std::string(constantText));
                if (constant.empty()) continue;
                const size_t index = record.colorBlendAttachments.size();
                (void)index;
                float parsed = 0.0f;
                try { parsed = std::stof(constant); }
                catch (...) { return false; }
                static_cast<void>(parsed);
            }
            std::vector<float> parsedConstants;
            for (const auto constantText : SplitArray(constants))
            {
                const std::string constant = Trim(std::string(constantText));
                if (constant.empty()) continue;
                try { parsedConstants.push_back(std::stof(constant)); }
                catch (...) { return false; }
            }
            for (size_t i = 0; i < std::min<size_t>(4, parsedConstants.size()); ++i)
                record.blendConstants[i] = parsedConstants[i];

            std::string_view attachments;
            if (!ExtractArray(colorBlend, "attachments", attachments))
                return false;
            for (const auto attachmentText : SplitArray(attachments))
            {
                const std::string attachmentEntry = Trim(std::string(attachmentText));
                if (attachmentEntry.empty()) continue;
                ColorBlendAttachmentRecord attachment;
                if (FindBool(attachmentEntry, "blend_enable", booleanValue))
                    attachment.blendEnable = booleanValue ? VK_TRUE : VK_FALSE;
                if (!FindUnsigned(attachmentEntry, "src_color_factor", value)) return false;
                attachment.srcColorBlendFactor = static_cast<VkBlendFactor>(value);
                if (!FindUnsigned(attachmentEntry, "dst_color_factor", value)) return false;
                attachment.dstColorBlendFactor = static_cast<VkBlendFactor>(value);
                if (!FindUnsigned(attachmentEntry, "color_op", value)) return false;
                attachment.colorBlendOp = static_cast<VkBlendOp>(value);
                if (!FindUnsigned(attachmentEntry, "src_alpha_factor", value)) return false;
                attachment.srcAlphaBlendFactor = static_cast<VkBlendFactor>(value);
                if (!FindUnsigned(attachmentEntry, "dst_alpha_factor", value)) return false;
                attachment.dstAlphaBlendFactor = static_cast<VkBlendFactor>(value);
                if (!FindUnsigned(attachmentEntry, "alpha_op", value)) return false;
                attachment.alphaBlendOp = static_cast<VkBlendOp>(value);
                if (!FindUnsigned(attachmentEntry, "color_write_mask", value)) return false;
                attachment.colorWriteMask = static_cast<VkColorComponentFlags>(value);
                record.colorBlendAttachments.push_back(attachment);
            }

            bool pnext = false;
            if (FindBool(colorBlend, "pnext_present", pnext) && pnext)
                record.replayCompatible = false;
        }

        std::string_view dynamicState;
        if (ExtractObject(entry, "dynamic_state", dynamicState))
        {
            std::string_view states;
            if (!ExtractArray(dynamicState, "states", states))
                return false;
            for (const auto stateText : SplitArray(states))
            {
                const std::string stateEntry = Trim(std::string(stateText));
                if (stateEntry.empty()) continue;
                try { record.dynamicStates.push_back(
                    static_cast<VkDynamicState>(std::stoul(stateEntry))); }
                catch (...) { return false; }
            }

            bool pnext = false;
            if (FindBool(dynamicState, "pnext_present", pnext) && pnext)
                record.replayCompatible = false;
        }

        recording.graphicsPipelinesToReplay.push_back(std::move(record));
    }

    return true;
}

bool ParseRecording(const std::string& path, Recording& recording, std::string& error)
{
    std::ifstream input(path);
    if (!input)
    {
        error = "could not open recording";
        return false;
    }

    std::string line;
    while (std::getline(input, line))
    {
        if (line.empty())
            continue;

        std::string event;
        if (!FindString(line, "event", event))
            continue;

        uint64_t hash = 0;
        if (event == "shader_module_code")
        {
            if (!FindHex(line, "hash", hash))
                continue;

            std::string encoded;
            if (!FindString(line, "code_base64", encoded))
            {
                error = "shader_module_code is missing code_base64";
                return false;
            }

            std::vector<uint8_t> bytes;
            if (!DecodeBase64(encoded, bytes) ||
                bytes.size() % sizeof(uint32_t) != 0)
            {
                error = "invalid Base64 SPIR-V payload";
                return false;
            }

            ShaderRecord shader;
            shader.words.resize(bytes.size() / sizeof(uint32_t));
            std::copy(bytes.begin(), bytes.end(),
                      reinterpret_cast<uint8_t*>(shader.words.data()));
            if (shader.words.empty() || shader.words[0] != 0x07230203u)
            {
                error = "recorded shader does not have a valid SPIR-V magic";
                return false;
            }
            recording.shaders[hash] = std::move(shader);
            continue;
        }

        if (event == "descriptor_set_layout_create")
        {
            if (!FindHex(line, "hash", hash))
                continue;

            DescriptorLayoutRecord record;
            uint64_t flags = 0;
            FindUnsigned(line, "flags", flags);
            record.flags = static_cast<VkDescriptorSetLayoutCreateFlags>(flags);

            std::string_view bindings;
            if (!ExtractArray(line, "bindings", bindings))
            {
                error = "descriptor_set_layout_create is missing bindings";
                return false;
            }

            for (const auto entryText : SplitArray(bindings))
            {
                const std::string entry = Trim(std::string(entryText));
                if (entry.empty())
                    continue;

                DescriptorBindingRecord binding;
                uint64_t value = 0;
                if (!FindUnsigned(entry, "binding", value))
                    return false;
                binding.binding = static_cast<uint32_t>(value);
                if (!FindUnsigned(entry, "descriptor_type", value))
                    return false;
                binding.descriptorType = static_cast<VkDescriptorType>(value);
                if (!FindUnsigned(entry, "descriptor_count", value))
                    return false;
                binding.descriptorCount = static_cast<uint32_t>(value);
                if (!FindUnsigned(entry, "stage_flags", value))
                    return false;
                binding.stageFlags = static_cast<VkShaderStageFlags>(value);
                if (!FindUnsigned(entry, "immutable_sampler_count", value))
                    return false;
                binding.immutableSamplerCount = static_cast<uint32_t>(value);
                record.bindings.push_back(binding);
            }

            recording.descriptorLayouts[hash] = std::move(record);
            continue;
        }

        if (event == "pipeline_layout_create")
        {
            if (!FindHex(line, "hash", hash))
                continue;

            PipelineLayoutRecord record;
            uint64_t value = 0;
            FindUnsigned(line, "flags", value);
            record.flags = static_cast<VkPipelineLayoutCreateFlags>(value);

            std::string_view setLayouts;
            if (!ExtractArray(line, "set_layouts", setLayouts))
            {
                error = "pipeline_layout_create is missing set_layouts";
                return false;
            }
            for (const auto valueText : SplitArray(setLayouts))
            {
                const std::string item = Trim(std::string(valueText));
                if (item.empty())
                    continue;
                try
                {
                    std::string normalized = item;
                    if (normalized.size() >= 2 && normalized.front() == '"' && normalized.back() == '"')
                        normalized = normalized.substr(1, normalized.size() - 2);
                    record.setLayoutHashes.push_back(std::stoull(normalized, nullptr, 16));
                }
                catch (...)
                {
                    error = "invalid descriptor layout hash";
                    return false;
                }
            }

            std::string_view pushConstants;
            if (!ExtractArray(line, "push_constants", pushConstants))
            {
                error = "pipeline_layout_create is missing push_constants";
                return false;
            }
            for (const auto valueText : SplitArray(pushConstants))
            {
                const std::string entry = Trim(std::string(valueText));
                if (entry.empty())
                    continue;

                PushConstantRecord range;
                if (!FindUnsigned(entry, "stage_flags", value))
                    return false;
                range.stageFlags = static_cast<VkShaderStageFlags>(value);
                if (!FindUnsigned(entry, "offset", value))
                    return false;
                range.offset = static_cast<uint32_t>(value);
                if (!FindUnsigned(entry, "size", value))
                    return false;
                range.size = static_cast<uint32_t>(value);
                record.pushConstants.push_back(range);
            }

            recording.pipelineLayouts[hash] = std::move(record);
            continue;
        }

        if (event == "render_pass_create")
        {
            if (!FindHex(line, "hash", hash))
                continue;

            RenderPassRecord record;
            uint64_t value = 0;
            FindUnsigned(line, "flags", value);
            record.flags = static_cast<VkRenderPassCreateFlags>(value);

            std::string compatible;
            size_t compatibleStart = 0;
            if (FindToken(line, "replay_compatible", compatibleStart))
            {
                record.replayCompatible = line.substr(compatibleStart, 4) == "true";
            }

            std::string_view attachments;
            if (!ExtractArray(line, "attachments", attachments))
            {
                error = "render_pass_create is missing attachments";
                return false;
            }

            for (const auto attachmentText : SplitArray(attachments))
            {
                const std::string entry = Trim(std::string(attachmentText));
                if (entry.empty())
                    continue;

                RenderPassAttachmentRecord attachment;
                if (!FindUnsigned(entry, "flags", value)) return false;
                attachment.description.flags = static_cast<VkAttachmentDescriptionFlags>(value);
                if (!FindUnsigned(entry, "format", value)) return false;
                attachment.description.format = static_cast<VkFormat>(value);
                if (!FindUnsigned(entry, "samples", value)) return false;
                attachment.description.samples = static_cast<VkSampleCountFlagBits>(value);
                if (!FindUnsigned(entry, "load_op", value)) return false;
                attachment.description.loadOp = static_cast<VkAttachmentLoadOp>(value);
                if (!FindUnsigned(entry, "store_op", value)) return false;
                attachment.description.storeOp = static_cast<VkAttachmentStoreOp>(value);
                if (!FindUnsigned(entry, "stencil_load_op", value)) return false;
                attachment.description.stencilLoadOp = static_cast<VkAttachmentLoadOp>(value);
                if (!FindUnsigned(entry, "stencil_store_op", value)) return false;
                attachment.description.stencilStoreOp = static_cast<VkAttachmentStoreOp>(value);
                if (!FindUnsigned(entry, "initial_layout", value)) return false;
                attachment.description.initialLayout = static_cast<VkImageLayout>(value);
                if (!FindUnsigned(entry, "final_layout", value)) return false;
                attachment.description.finalLayout = static_cast<VkImageLayout>(value);
                record.attachments.push_back(attachment);
            }

            std::string_view subpasses;
            if (!ExtractArray(line, "subpasses", subpasses))
            {
                error = "render_pass_create is missing subpasses";
                return false;
            }

            for (const auto subpassText : SplitArray(subpasses))
            {
                const std::string entry = Trim(std::string(subpassText));
                if (entry.empty())
                    continue;

                RenderPassSubpassRecord subpass;
                if (!FindUnsigned(entry, "flags", value)) return false;
                subpass.flags = static_cast<VkSubpassDescriptionFlags>(value);
                if (!FindUnsigned(entry, "pipeline_bind_point", value)) return false;
                subpass.pipelineBindPoint = static_cast<VkPipelineBindPoint>(value);

                auto parseRefs = [&](std::string_view key, std::vector<RenderPassReferenceRecord>& output) -> bool
                {
                    std::string_view refs;
                    if (!ExtractArray(entry, key, refs))
                        return false;
                    for (const auto refText : SplitArray(refs))
                    {
                        const std::string refEntry = Trim(std::string(refText));
                        if (refEntry.empty())
                            continue;
                        RenderPassReferenceRecord reference;
                        uint64_t refValue = 0;
                        if (!FindUnsigned(refEntry, "attachment", refValue)) return false;
                        reference.attachment = static_cast<uint32_t>(refValue);
                        if (!FindUnsigned(refEntry, "layout", refValue)) return false;
                        reference.layout = static_cast<VkImageLayout>(refValue);
                        output.push_back(reference);
                    }
                    return true;
                };

                if (!parseRefs("input_attachments", subpass.inputAttachments) ||
                    !parseRefs("color_attachments", subpass.colorAttachments) ||
                    !parseRefs("resolve_attachments", subpass.resolveAttachments))
                {
                    error = "render_pass_create has invalid attachment references";
                    return false;
                }

                size_t depthStart = 0;
                if (!FindToken(entry, "depth_stencil", depthStart))
                    return false;
                while (depthStart < entry.size() && std::isspace(static_cast<unsigned char>(entry[depthStart])))
                    ++depthStart;
                if (depthStart < entry.size() && entry[depthStart] == '{')
                {
                    std::string_view depth;
                    if (!ExtractObject(entry, "depth_stencil", depth))
                        return false;
                    uint64_t refValue = 0;
                    if (!FindUnsigned(depth, "attachment", refValue)) return false;
                    subpass.depthStencil.attachment = static_cast<uint32_t>(refValue);
                    if (!FindUnsigned(depth, "layout", refValue)) return false;
                    subpass.depthStencil.layout = static_cast<VkImageLayout>(refValue);
                    subpass.hasDepthStencil = true;
                }

                std::string_view preserve;
                if (!ExtractArray(entry, "preserve_attachments", preserve))
                    return false;
                for (const auto preserveText : SplitArray(preserve))
                {
                    const std::string item = Trim(std::string(preserveText));
                    if (item.empty()) continue;
                    if (!FindUnsigned(item, "value", value))
                    {
                        try { subpass.preserveAttachments.push_back(
                            static_cast<uint32_t>(std::stoull(item))); }
                        catch (...) { return false; }
                    }
                }

                record.subpasses.push_back(std::move(subpass));
            }

            std::string_view dependencies;
            if (!ExtractArray(line, "dependencies", dependencies))
            {
                error = "render_pass_create is missing dependencies";
                return false;
            }
            for (const auto dependencyText : SplitArray(dependencies))
            {
                const std::string entry = Trim(std::string(dependencyText));
                if (entry.empty())
                    continue;

                RenderPassDependencyRecord dependency;
                if (!FindUnsigned(entry, "src_subpass", value)) return false;
                dependency.srcSubpass = static_cast<uint32_t>(value);
                if (!FindUnsigned(entry, "dst_subpass", value)) return false;
                dependency.dstSubpass = static_cast<uint32_t>(value);
                if (!FindUnsigned(entry, "src_stage_mask", value)) return false;
                dependency.srcStageMask = static_cast<VkPipelineStageFlags>(value);
                if (!FindUnsigned(entry, "dst_stage_mask", value)) return false;
                dependency.dstStageMask = static_cast<VkPipelineStageFlags>(value);
                if (!FindUnsigned(entry, "src_access_mask", value)) return false;
                dependency.srcAccessMask = static_cast<VkAccessFlags>(value);
                if (!FindUnsigned(entry, "dst_access_mask", value)) return false;
                dependency.dstAccessMask = static_cast<VkAccessFlags>(value);
                if (!FindUnsigned(entry, "dependency_flags", value)) return false;
                dependency.dependencyFlags = static_cast<VkDependencyFlags>(value);
                record.dependencies.push_back(dependency);
            }

            recording.renderPasses[hash] = std::move(record);
            continue;
        }

        if (event == "compute_pipeline_state")
        {
            std::string_view pipelines;
            if (!ExtractArray(line, "pipelines", pipelines))
            {
                error = "compute_pipeline_state is missing pipelines";
                return false;
            }

            for (const auto valueText : SplitArray(pipelines))
            {
                const std::string entry = Trim(std::string(valueText));
                if (entry.empty())
                    continue;

                ComputePipelineRecord pipeline;
                uint64_t value = 0;
                if (!FindHex(entry, "layout_hash", pipeline.layoutHash))
                    return false;
                if (!FindHex(entry, "module_hash", pipeline.moduleHash))
                    return false;
                if (!FindUnsigned(entry, "flags", value))
                    return false;
                pipeline.flags = static_cast<VkPipelineCreateFlags>(value);
                if (FindUnsigned(entry, "stage_flags", value))
                    pipeline.stageFlags = static_cast<VkPipelineShaderStageCreateFlags>(value);
                FindString(entry, "entry_point", pipeline.entryPoint);

                std::string_view specialization;
                if (ExtractObject(entry, "specialization", specialization))
                {
                    pipeline.specialization.present = true;
                    std::string encoded;
                    FindString(specialization, "data_base64", encoded);
                    if (!encoded.empty() &&
                        !DecodeBase64(encoded, pipeline.specialization.data))
                    {
                        error = "invalid specialization Base64 payload";
                        return false;
                    }

                    std::string_view mapEntries;
                    if (!ExtractArray(specialization, "map_entries", mapEntries))
                    {
                        error = "specialization is missing map_entries";
                        return false;
                    }

                    for (const auto mapText : SplitArray(mapEntries))
                    {
                        const std::string mapEntry = Trim(std::string(mapText));
                        if (mapEntry.empty())
                            continue;

                        VkSpecializationMapEntry map{};
                        if (!FindUnsigned(mapEntry, "constant_id", value))
                            return false;
                        map.constantID = static_cast<uint32_t>(value);
                        if (!FindUnsigned(mapEntry, "offset", value))
                            return false;
                        map.offset = static_cast<size_t>(value);
                        if (!FindUnsigned(mapEntry, "size", value))
                            return false;
                        map.size = static_cast<size_t>(value);
                        pipeline.specialization.mapEntries.push_back(map);
                    }
                }

                recording.computePipelines.push_back(std::move(pipeline));
            }
            continue;
        }

        if (event == "graphics_pipeline_state")
        {
            uint64_t count = 0;
            if (FindUnsigned(line, "count", count))
                recording.graphicsPipelines += static_cast<size_t>(count);

            if (!ParseGraphicsPipelineState(line, recording, error))
                return false;
            continue;
        }

        if (event == "ray_tracing_pipeline_create")
        {
            uint64_t count = 0;
            if (FindUnsigned(line, "count", count))
                recording.rayTracingPipelines += static_cast<size_t>(count);
        }
    }

    return true;
}

bool Check(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
    {
        std::cerr << operation << " failed: " << result << "\\n";
        return false;
    }
    return true;
}

bool ReadBinaryFile(const std::string& path, std::vector<uint8_t>& data)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return false;

    const std::streamsize size = file.tellg();
    if (size <= 0)
        return false;

    file.seekg(0, std::ios::beg);
    data.resize(static_cast<size_t>(size));
    return file.read(reinterpret_cast<char*>(data.data()), size).good();
}

bool WriteBinaryFile(const std::string& path, const void* data, size_t size)
{
    std::ofstream file(path, std::ios::binary);
    if (!file)
        return false;

    file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return file.good();
}

void DestroyContext(VkContext& context,
                    std::unordered_map<uint64_t, VkDescriptorSetLayout>& descriptorLayouts,
                    std::unordered_map<uint64_t, VkPipelineLayout>& pipelineLayouts,
                    std::unordered_map<uint64_t, VkShaderModule>& shaderModules,
                    std::unordered_map<uint64_t, VkRenderPass>& renderPasses)
{
    for (const auto& [hash, pipelineLayout] : pipelineLayouts)
        vkDestroyPipelineLayout(context.device, pipelineLayout, nullptr);
    for (const auto& [hash, descriptorLayout] : descriptorLayouts)
        vkDestroyDescriptorSetLayout(context.device, descriptorLayout, nullptr);
    for (const auto& [hash, shaderModule] : shaderModules)
        vkDestroyShaderModule(context.device, shaderModule, nullptr);
    for (const auto& [hash, renderPass] : renderPasses)
        vkDestroyRenderPass(context.device, renderPass, nullptr);

    if (context.cache && context.device)
        vkDestroyPipelineCache(context.device, context.cache, nullptr);
    if (context.device)
        vkDestroyDevice(context.device, nullptr);
    if (context.instance)
        vkDestroyInstance(context.instance, nullptr);

    context = {};
}

void DestroyContext(VkContext& context)
{
    if (context.cache && context.device)
        vkDestroyPipelineCache(context.device, context.cache, nullptr);
    if (context.device)
        vkDestroyDevice(context.device, nullptr);
    if (context.instance)
        vkDestroyInstance(context.instance, nullptr);
    context = {};
}

int Run(const std::string& recordingPath,
        const std::string& inputCachePath,
        const std::string& outputCachePath)
{
    Recording recording;
    std::string error;
    if (!ParseRecording(recordingPath, recording, error))
    {
        std::cerr << "Recording parse failed: " << error << "\\n";
        return 1;
    }

    std::cout << "Shaders: " << recording.shaders.size()
              << ", descriptor layouts: " << recording.descriptorLayouts.size()
              << ", pipeline layouts: " << recording.pipelineLayouts.size()
              << ", render passes: " << recording.renderPasses.size()
              << ", compute pipelines: " << recording.computePipelines.size()
              << ", graphics pipelines skipped: " << recording.graphicsPipelines
              << ", ray-tracing pipelines skipped: " << recording.rayTracingPipelines << "\\n";

    VkContext context;

    uint32_t loaderApiVersion = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion(&loaderApiVersion) != VK_SUCCESS)
        loaderApiVersion = VK_API_VERSION_1_0;
    const uint32_t requestedApiVersion =
        std::min(loaderApiVersion, VK_API_VERSION_1_3);

    VkApplicationInfo app{
        VK_STRUCTURE_TYPE_APPLICATION_INFO,
        nullptr,
        "SCSKiller Vulkan Warmer",
        VK_MAKE_VERSION(1, 0, 0),
        "SCSKiller",
        VK_MAKE_VERSION(1, 0, 0),
        requestedApiVersion
    };
    VkInstanceCreateInfo instanceInfo{
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        nullptr,
        0,
        &app,
        0,
        nullptr,
        0,
        nullptr
    };
    if (!Check(vkCreateInstance(&instanceInfo, nullptr, &context.instance), "vkCreateInstance"))
        return 1;

    uint32_t deviceCount = 0;
    if (!Check(vkEnumeratePhysicalDevices(context.instance, &deviceCount, nullptr),
               "vkEnumeratePhysicalDevices(count)") || deviceCount == 0)
    {
        std::cerr << "No Vulkan physical devices available\\n";
        DestroyContext(context);
        return 1;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    if (!Check(vkEnumeratePhysicalDevices(context.instance, &deviceCount, devices.data()),
               "vkEnumeratePhysicalDevices"))
    {
        DestroyContext(context);
        return 1;
    }
    context.physicalDevice = devices[0];

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(context.physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queues(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(
        context.physicalDevice, &queueFamilyCount, queues.data());

    uint32_t queueFamily = VK_QUEUE_FAMILY_IGNORED;
    for (uint32_t i = 0; i < queueFamilyCount; ++i)
    {
        if (queues[i].queueCount > 0 &&
            (queues[i].queueFlags & (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT)))
        {
            queueFamily = i;
            break;
        }
    }
    if (queueFamily == VK_QUEUE_FAMILY_IGNORED)
    {
        std::cerr << "No graphics/compute queue family available\\n";
        DestroyContext(context);
        return 1;
    }

    VkPhysicalDeviceProperties physicalProperties{};
    vkGetPhysicalDeviceProperties(context.physicalDevice, &physicalProperties);

    VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeature{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
        nullptr,
        VK_FALSE
    };

    if (requestedApiVersion >= VK_API_VERSION_1_3 &&
        physicalProperties.apiVersion >= VK_API_VERSION_1_3)
    {
        VkPhysicalDeviceFeatures2 queriedFeatures{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            &dynamicRenderingFeature,
            {}
        };
        auto getFeatures2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            vkGetInstanceProcAddr(context.instance, "vkGetPhysicalDeviceFeatures2"));
        if (getFeatures2)
        {
            getFeatures2(context.physicalDevice, &queriedFeatures);
            context.dynamicRenderingEnabled =
                dynamicRenderingFeature.dynamicRendering == VK_TRUE;
        }
    }

    VkPhysicalDeviceFeatures availableFeatures{};
    vkGetPhysicalDeviceFeatures(context.physicalDevice, &availableFeatures);

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        nullptr,
        0,
        queueFamily,
        1,
        &priority
    };

    // Enable every core Vulkan 1.0 feature exposed by the selected device.
    // This avoids artificially disabling capabilities that the captured
    // pipeline may have relied upon in the original application.
    VkDeviceCreateInfo deviceInfo{
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        context.dynamicRenderingEnabled ? &dynamicRenderingFeature : nullptr,
        0,
        1,
        &queueInfo,
        0,
        nullptr,
        0,
        nullptr,
        &availableFeatures
    };
    if (!Check(vkCreateDevice(context.physicalDevice, &deviceInfo, nullptr, &context.device),
               "vkCreateDevice"))
    {
        DestroyContext(context);
        return 1;
    }

    std::vector<uint8_t> inputCache;
    VkPipelineCacheCreateInfo cacheInfo{
        VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        nullptr,
        0,
        0,
        nullptr
    };
    if (!inputCachePath.empty() && inputCachePath != "-")
    {
        if (!ReadBinaryFile(inputCachePath, inputCache))
            std::cerr << "Warning: could not read input cache; starting empty\\n";
        else
        {
            cacheInfo.initialDataSize = inputCache.size();
            cacheInfo.pInitialData = inputCache.data();
        }
    }

    VkResult cacheResult = vkCreatePipelineCache(
        context.device, &cacheInfo, nullptr, &context.cache);
    if (cacheResult != VK_SUCCESS && cacheInfo.initialDataSize != 0)
    {
        std::cerr << "Warning: input pipeline cache was rejected by this device; starting empty\\n";
        cacheInfo.initialDataSize = 0;
        cacheInfo.pInitialData = nullptr;
        cacheResult = vkCreatePipelineCache(
            context.device, &cacheInfo, nullptr, &context.cache);
    }
    if (!Check(cacheResult, "vkCreatePipelineCache"))
    {
        DestroyContext(context);
        return 1;
    }

    std::unordered_map<uint64_t, VkDescriptorSetLayout> descriptorLayouts;
    std::unordered_map<uint64_t, VkPipelineLayout> pipelineLayouts;
    std::unordered_map<uint64_t, VkShaderModule> shaderModules;
    std::unordered_map<uint64_t, VkRenderPass> renderPasses;

    for (const auto& [hash, record] : recording.descriptorLayouts)
    {
        bool unsupported = false;
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        bindings.reserve(record.bindings.size());
        for (const auto& binding : record.bindings)
        {
            if (binding.immutableSamplerCount != 0)
            {
                unsupported = true;
                break;
            }

            bindings.push_back(VkDescriptorSetLayoutBinding{
                binding.binding,
                binding.descriptorType,
                binding.descriptorCount,
                binding.stageFlags,
                nullptr
            });
        }

        if (unsupported)
        {
            std::cerr << "Skipping descriptor layout " << std::hex << hash << std::dec
                      << ": immutable samplers are not yet reconstructible\\n";
            continue;
        }

        VkDescriptorSetLayoutCreateInfo info{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            nullptr,
            record.flags,
            static_cast<uint32_t>(bindings.size()),
            bindings.data()
        };
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        if (!Check(vkCreateDescriptorSetLayout(
                       context.device, &info, nullptr, &layout),
                   "vkCreateDescriptorSetLayout"))
        {
            DestroyContext(context, descriptorLayouts, pipelineLayouts, shaderModules, renderPasses);
            return 1;
        }
        descriptorLayouts.emplace(hash, layout);
    }

    for (const auto& [hash, record] : recording.renderPasses)
    {
        if (!record.replayCompatible)
        {
            std::cerr << "Skipping render pass " << std::hex << hash << std::dec
                      << ": capture contains unsupported pNext state\n";
            continue;
        }

        std::vector<VkAttachmentDescription> attachments;
        attachments.reserve(record.attachments.size());
        for (const auto& attachment : record.attachments)
            attachments.push_back(attachment.description);

        std::vector<std::vector<VkAttachmentReference>> inputRefs(record.subpasses.size());
        std::vector<std::vector<VkAttachmentReference>> colorRefs(record.subpasses.size());
        std::vector<std::vector<VkAttachmentReference>> resolveRefs(record.subpasses.size());
        std::vector<std::vector<uint32_t>> preserveRefs(record.subpasses.size());
        std::vector<VkSubpassDescription> subpasses(record.subpasses.size());

        for (size_t i = 0; i < record.subpasses.size(); ++i)
        {
            const auto& source = record.subpasses[i];
            auto& input = inputRefs[i];
            auto& color = colorRefs[i];
            auto& resolve = resolveRefs[i];
            auto& preserve = preserveRefs[i];

            for (const auto& reference : source.inputAttachments)
                input.push_back({reference.attachment, reference.layout});
            for (const auto& reference : source.colorAttachments)
                color.push_back({reference.attachment, reference.layout});
            for (const auto& reference : source.resolveAttachments)
                resolve.push_back({reference.attachment, reference.layout});
            preserve = source.preserveAttachments;

            VkSubpassDescription& destination = subpasses[i];
            destination = {
                source.flags,
                source.pipelineBindPoint,
                static_cast<uint32_t>(input.size()),
                input.data(),
                static_cast<uint32_t>(color.size()),
                color.data(),
                resolve.empty() ? nullptr : resolve.data(),
                nullptr,
                static_cast<uint32_t>(preserve.size()),
                preserve.data()
            };

            if (source.hasDepthStencil)
            {
                static VkAttachmentReference dummy{};
                dummy = {source.depthStencil.attachment, source.depthStencil.layout};
                destination.pDepthStencilAttachment = &dummy;
            }
        }

        std::vector<VkAttachmentReference> depthReferences;
        depthReferences.reserve(record.subpasses.size());
        for (size_t i = 0; i < record.subpasses.size(); ++i)
        {
            if (!record.subpasses[i].hasDepthStencil)
                continue;
            depthReferences.push_back({
                record.subpasses[i].depthStencil.attachment,
                record.subpasses[i].depthStencil.layout
            });
            subpasses[i].pDepthStencilAttachment = &depthReferences.back();
        }

        std::vector<VkSubpassDependency> dependencies;
        dependencies.reserve(record.dependencies.size());
        for (const auto& dependency : record.dependencies)
        {
            dependencies.push_back({
                dependency.srcSubpass,
                dependency.dstSubpass,
                dependency.srcStageMask,
                dependency.dstStageMask,
                dependency.srcAccessMask,
                dependency.dstAccessMask,
                dependency.dependencyFlags
            });
        }

        VkRenderPassCreateInfo info{
            VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            nullptr,
            record.flags,
            static_cast<uint32_t>(attachments.size()),
            attachments.data(),
            static_cast<uint32_t>(subpasses.size()),
            subpasses.data(),
            static_cast<uint32_t>(dependencies.size()),
            dependencies.data()
        };

        VkRenderPass renderPass = VK_NULL_HANDLE;
        if (!Check(vkCreateRenderPass(
                       context.device, &info, nullptr, &renderPass),
                   "vkCreateRenderPass"))
        {
            DestroyContext(context, descriptorLayouts, pipelineLayouts, shaderModules, renderPasses);
            return 1;
        }

        renderPasses.emplace(hash, renderPass);
    }

    for (const auto& [hash, record] : recording.pipelineLayouts)
    {
        std::vector<VkDescriptorSetLayout> setLayouts;
        bool unsupported = false;
        for (const uint64_t setHash : record.setLayoutHashes)
        {
            const auto it = descriptorLayouts.find(setHash);
            if (it == descriptorLayouts.end())
            {
                unsupported = true;
                break;
            }
            setLayouts.push_back(it->second);
        }

        if (unsupported)
        {
            std::cerr << "Skipping pipeline layout " << std::hex << hash << std::dec
                      << ": referenced descriptor layout is not reconstructible\\n";
            continue;
        }

        std::vector<VkPushConstantRange> pushConstants;
        pushConstants.reserve(record.pushConstants.size());
        for (const auto& range : record.pushConstants)
        {
            pushConstants.push_back(VkPushConstantRange{
                range.stageFlags,
                range.offset,
                range.size
            });
        }

        VkPipelineLayoutCreateInfo info{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            nullptr,
            record.flags,
            static_cast<uint32_t>(setLayouts.size()),
            setLayouts.data(),
            static_cast<uint32_t>(pushConstants.size()),
            pushConstants.data()
        };
        VkPipelineLayout layout = VK_NULL_HANDLE;
        if (!Check(vkCreatePipelineLayout(
                       context.device, &info, nullptr, &layout),
                   "vkCreatePipelineLayout"))
        {
            DestroyContext(context, descriptorLayouts, pipelineLayouts, shaderModules, renderPasses);
            return 1;
        }
        pipelineLayouts.emplace(hash, layout);
    }

    for (const auto& [hash, record] : recording.shaders)
    {
        VkShaderModuleCreateInfo info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            nullptr,
            0,
            record.words.size() * sizeof(uint32_t),
            record.words.data()
        };
        VkShaderModule module = VK_NULL_HANDLE;
        if (!Check(vkCreateShaderModule(
                       context.device, &info, nullptr, &module),
                   "vkCreateShaderModule"))
        {
            DestroyContext(context, descriptorLayouts, pipelineLayouts, shaderModules, renderPasses);
            return 1;
        }
        shaderModules.emplace(hash, module);
    }

    size_t succeeded = 0;
    size_t skipped = 0;
    size_t failed = 0;

    for (const auto& record : recording.computePipelines)
    {
        const auto layoutIt = pipelineLayouts.find(record.layoutHash);
        const auto shaderIt = shaderModules.find(record.moduleHash);
        if (layoutIt == pipelineLayouts.end() || shaderIt == shaderModules.end())
        {
            ++skipped;
            std::cerr << "Skipping compute pipeline: missing reconstructible layout or shader "
                      << std::hex << record.moduleHash << std::dec << "\\n";
            continue;
        }

        VkSpecializationInfo specializationInfo{};
        if (record.specialization.present)
        {
            specializationInfo.mapEntryCount =
                static_cast<uint32_t>(record.specialization.mapEntries.size());
            specializationInfo.pMapEntries = record.specialization.mapEntries.data();
            specializationInfo.dataSize = record.specialization.data.size();
            specializationInfo.pData = record.specialization.data.data();
        }

        VkPipelineShaderStageCreateInfo stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            record.stageFlags,
            VK_SHADER_STAGE_COMPUTE_BIT,
            shaderIt->second,
            record.entryPoint.c_str(),
            record.specialization.present ? &specializationInfo : nullptr
        };
        VkComputePipelineCreateInfo info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            nullptr,
            record.flags,
            stage,
            layoutIt->second,
            VK_NULL_HANDLE,
            -1
        };

        VkPipeline pipeline = VK_NULL_HANDLE;
        const VkResult result = vkCreateComputePipelines(
            context.device, context.cache, 1, &info, nullptr, &pipeline);
        if (result == VK_SUCCESS)
        {
            ++succeeded;
            vkDestroyPipeline(context.device, pipeline, nullptr);
        }
        else
        {
            ++failed;
            std::cerr << "Compute pipeline replay failed: " << result << "\\n";
        }
    }


    size_t graphicsSucceeded = 0;
    size_t graphicsSkipped = 0;
    size_t graphicsFailed = 0;

    for (const auto& record : recording.graphicsPipelinesToReplay)
    {
        if (!record.replayCompatible)
        {
            ++graphicsSkipped;
            std::cerr << "Skipping graphics pipeline: capture contains unsupported state\n";
            continue;
        }

        const auto layoutIt = pipelineLayouts.find(record.layoutHash);
        const auto renderPassIt = renderPasses.find(record.renderPassHash);
        if (layoutIt == pipelineLayouts.end() ||
            record.stages.empty())
        {
            ++graphicsSkipped;
            std::cerr << "Skipping graphics pipeline: missing layout or shader stage\n";
            continue;
        }

        if (record.dynamicRendering && !context.dynamicRenderingEnabled)
        {
            ++graphicsSkipped;
            std::cerr << "Skipping dynamic-rendering graphics pipeline: Vulkan dynamicRendering feature is unavailable\n";
            continue;
        }

        if (!record.dynamicRendering && renderPassIt == renderPasses.end())
        {
            ++graphicsSkipped;
            std::cerr << "Skipping graphics pipeline: render pass is unavailable\n";
            continue;
        }

        if (record.basePipelineIndex >= 0)
        {
            ++graphicsSkipped;
            std::cerr << "Skipping graphics pipeline: derivative base-pipeline replay is not yet supported\n";
            continue;
        }

        bool dynamicViewport = false;
        bool dynamicScissor = false;
        for (const VkDynamicState state : record.dynamicStates)
        {
            dynamicViewport |= state == VK_DYNAMIC_STATE_VIEWPORT;
            dynamicScissor |= state == VK_DYNAMIC_STATE_SCISSOR;
        }

        if (!record.hasVertexInput ||
            !record.hasInputAssembly ||
            !record.hasRasterization ||
            !record.hasMultisample ||
            !record.hasViewportState)
        {
            ++graphicsSkipped;
            std::cerr << "Skipping graphics pipeline: required fixed-function state was not captured\n";
            continue;
        }

        if ((!dynamicViewport &&
             record.viewports.size() != record.viewportCount) ||
            (!dynamicScissor &&
             record.scissors.size() != record.scissorCount))
        {
            ++graphicsSkipped;
            std::cerr << "Skipping graphics pipeline: static viewport/scissor data is incomplete\n";
            continue;
        }

        const RenderPassSubpassRecord* subpassRecord = nullptr;
        if (!record.dynamicRendering)
        {
            const auto renderPassRecordIt = recording.renderPasses.find(record.renderPassHash);
            if (renderPassRecordIt == recording.renderPasses.end() ||
                record.subpass >= renderPassRecordIt->second.subpasses.size())
            {
                ++graphicsSkipped;
                std::cerr << "Skipping graphics pipeline: render-pass subpass is unavailable\n";
                continue;
            }
            subpassRecord = &renderPassRecordIt->second.subpasses[record.subpass];
        }

        const bool needsDepthStencil = record.dynamicRendering
            ? record.depthFormat != VK_FORMAT_UNDEFINED || record.stencilFormat != VK_FORMAT_UNDEFINED
            : subpassRecord->hasDepthStencil;
        const bool needsColorBlend = record.dynamicRendering
            ? !record.colorFormats.empty()
            : !subpassRecord->colorAttachments.empty();

        if ((needsDepthStencil && !record.hasDepthStencil) ||
            (needsColorBlend && !record.hasColorBlend))
        {
            ++graphicsSkipped;
            std::cerr << "Skipping graphics pipeline: attachment-dependent state is incomplete\n";
            continue;
        }

        std::vector<VkPipelineShaderStageCreateInfo> stages;
        stages.reserve(record.stages.size());
        std::vector<VkSpecializationInfo> specializations(record.stages.size());
        for (size_t i = 0; i < record.stages.size(); ++i)
        {
            const auto& source = record.stages[i];
            const auto shaderIt = shaderModules.find(source.moduleHash);
            if (shaderIt == shaderModules.end())
            {
                stages.clear();
                break;
            }

            VkSpecializationInfo* specializationInfo = nullptr;
            if (source.specialization.present)
            {
                specializations[i].mapEntryCount =
                    static_cast<uint32_t>(source.specialization.mapEntries.size());
                specializations[i].pMapEntries = source.specialization.mapEntries.data();
                specializations[i].dataSize = source.specialization.data.size();
                specializations[i].pData = source.specialization.data.data();
                specializationInfo = &specializations[i];
            }

            stages.push_back(VkPipelineShaderStageCreateInfo{
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                nullptr,
                source.flags,
                source.stage,
                shaderIt->second,
                source.entryPoint.c_str(),
                specializationInfo
            });
        }

        if (stages.size() != record.stages.size())
        {
            ++graphicsSkipped;
            std::cerr << "Skipping graphics pipeline: one or more shader modules are unavailable\n";
            continue;
        }

        std::vector<VkVertexInputBindingDescription> bindings;
        bindings.reserve(record.vertexBindings.size());
        for (const auto& binding : record.vertexBindings)
            bindings.push_back({
                binding.binding,
                binding.stride,
                binding.inputRate
            });

        std::vector<VkVertexInputAttributeDescription> attributes;
        attributes.reserve(record.vertexAttributes.size());
        for (const auto& attribute : record.vertexAttributes)
            attributes.push_back({
                attribute.location,
                attribute.binding,
                attribute.format,
                attribute.offset
            });

        VkPipelineVertexInputStateCreateInfo vertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            nullptr,
            0,
            static_cast<uint32_t>(bindings.size()),
            bindings.data(),
            static_cast<uint32_t>(attributes.size()),
            attributes.data()
        };

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            nullptr,
            0,
            record.topology,
            record.primitiveRestartEnable
        };

        std::vector<VkViewport> viewports;
        viewports.reserve(record.viewports.size());
        for (const auto& value : record.viewports)
            viewports.push_back({
                value.x,
                value.y,
                value.width,
                value.height,
                value.minDepth,
                value.maxDepth
            });

        std::vector<VkRect2D> scissors;
        scissors.reserve(record.scissors.size());
        for (const auto& value : record.scissors)
            scissors.push_back({
                {value.offsetX, value.offsetY},
                {value.width, value.height}
            });

        VkPipelineViewportStateCreateInfo viewportState{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            nullptr,
            0,
            record.viewportCount,
            dynamicViewport ? nullptr : viewports.data(),
            record.scissorCount,
            dynamicScissor ? nullptr : scissors.data()
        };

        VkPipelineRasterizationStateCreateInfo rasterization{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            nullptr,
            0,
            record.depthClampEnable,
            record.rasterizerDiscardEnable,
            record.polygonMode,
            record.cullMode,
            record.frontFace,
            record.depthBiasEnable,
            record.depthBiasConstantFactor,
            record.depthBiasClamp,
            record.depthBiasSlopeFactor,
            record.lineWidth
        };

        const VkSampleMask* sampleMask = record.sampleMask.empty()
            ? nullptr
            : record.sampleMask.data();

        VkPipelineMultisampleStateCreateInfo multisample{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            nullptr,
            0,
            record.rasterizationSamples,
            record.sampleShadingEnable,
            record.minSampleShading,
            sampleMask,
            record.alphaToCoverageEnable,
            record.alphaToOneEnable
        };

        VkStencilOpState frontStencil{
            record.frontStencil.failOp,
            record.frontStencil.passOp,
            record.frontStencil.depthFailOp,
            record.frontStencil.compareOp,
            record.frontStencil.compareMask,
            record.frontStencil.writeMask,
            record.frontStencil.reference
        };
        VkStencilOpState backStencil{
            record.backStencil.failOp,
            record.backStencil.passOp,
            record.backStencil.depthFailOp,
            record.backStencil.compareOp,
            record.backStencil.compareMask,
            record.backStencil.writeMask,
            record.backStencil.reference
        };

        VkPipelineDepthStencilStateCreateInfo depthStencil{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            nullptr,
            0,
            record.depthTestEnable,
            record.depthWriteEnable,
            record.depthCompareOp,
            record.depthBoundsTestEnable,
            record.stencilTestEnable,
            frontStencil,
            backStencil,
            record.minDepthBounds,
            record.maxDepthBounds
        };

        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments;
        blendAttachments.reserve(record.colorBlendAttachments.size());
        for (const auto& attachment : record.colorBlendAttachments)
        {
            blendAttachments.push_back({
                attachment.blendEnable,
                attachment.srcColorBlendFactor,
                attachment.dstColorBlendFactor,
                attachment.colorBlendOp,
                attachment.srcAlphaBlendFactor,
                attachment.dstAlphaBlendFactor,
                attachment.alphaBlendOp,
                attachment.colorWriteMask
            });
        }

        VkPipelineColorBlendStateCreateInfo colorBlend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            nullptr,
            0,
            record.logicOpEnable,
            record.logicOp,
            static_cast<uint32_t>(blendAttachments.size()),
            blendAttachments.data(),
            {
                record.blendConstants[0],
                record.blendConstants[1],
                record.blendConstants[2],
                record.blendConstants[3]
            }
        };

        VkPipelineDynamicStateCreateInfo dynamicState{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            nullptr,
            0,
            static_cast<uint32_t>(record.dynamicStates.size()),
            record.dynamicStates.data()
        };

        VkPipelineTessellationStateCreateInfo tessellation{
            VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
            nullptr,
            0,
            record.patchControlPoints
        };

        VkPipelineRenderingCreateInfo renderingInfo{
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            nullptr,
            record.viewMask,
            static_cast<uint32_t>(record.colorFormats.size()),
            record.colorFormats.data(),
            record.depthFormat,
            record.stencilFormat
        };

        VkGraphicsPipelineCreateInfo graphicsInfo{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            record.dynamicRendering ? &renderingInfo : nullptr,
            record.flags,
            static_cast<uint32_t>(stages.size()),
            stages.data(),
            &vertexInput,
            &inputAssembly,
            record.hasTessellation ? &tessellation : nullptr,
            &viewportState,
            &rasterization,
            &multisample,
            record.hasDepthStencil ? &depthStencil : nullptr,
            record.hasColorBlend ? &colorBlend : nullptr,
            record.dynamicStates.empty() ? nullptr : &dynamicState,
            layoutIt->second,
            record.dynamicRendering ? VK_NULL_HANDLE : renderPassIt->second,
            record.dynamicRendering ? 0u : record.subpass,
            VK_NULL_HANDLE,
            -1
        };

        VkPipeline pipeline = VK_NULL_HANDLE;
        const VkResult result = vkCreateGraphicsPipelines(
            context.device, context.cache, 1, &graphicsInfo, nullptr, &pipeline);
        if (result == VK_SUCCESS)
        {
            ++graphicsSucceeded;
            vkDestroyPipeline(context.device, pipeline, nullptr);
        }
        else
        {
            ++graphicsFailed;
            std::cerr << "Graphics pipeline replay failed: " << result << "\n";
        }
    }

    std::cout << "Graphics replay: requested " << recording.graphicsPipelinesToReplay.size()
              << ", compiled " << graphicsSucceeded
              << ", skipped " << graphicsSkipped
              << ", failed " << graphicsFailed << "\n";

    size_t cacheSize = 0;
    std::vector<uint8_t> outputCache;
    if (Check(vkGetPipelineCacheData(
                  context.device, context.cache, &cacheSize, nullptr),
              "vkGetPipelineCacheData(size)") && cacheSize > 0)
    {
        outputCache.resize(cacheSize);
        if (!Check(vkGetPipelineCacheData(
                       context.device, context.cache, &cacheSize, outputCache.data()),
                   "vkGetPipelineCacheData(data)"))
        {
            outputCache.clear();
        }
    }

    if (!outputCachePath.empty() && !outputCache.empty())
    {
        if (!WriteBinaryFile(
                outputCachePath, outputCache.data(), outputCache.size()))
        {
            std::cerr << "Could not write output pipeline cache: "
                      << outputCachePath << "\\n";
            ++failed;
        }
        else
        {
            std::cout << "Wrote pipeline cache: " << outputCachePath
                      << " (" << outputCache.size() << " bytes)\\n";
        }
    }

    const size_t total = recording.computePipelines.size();
    std::cout << "Compute replay: requested " << total
              << ", compiled " << succeeded
              << ", skipped " << skipped
              << ", failed " << failed << "\\n";

    DestroyContext(context, descriptorLayouts, pipelineLayouts, shaderModules, renderPasses);
    return (failed == 0 && graphicsFailed == 0) ? 0 : 1;
}
}

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 4)
    {
        std::cerr << "Usage: scskiller-vulkan-warmer <capture.jsonl> [input-cache.bin] [output-cache.bin]\\n";
        return 2;
    }

    return Run(
        argv[1],
        argc >= 3 ? argv[2] : std::string{},
        argc >= 4 ? argv[3] : std::string{});
}
