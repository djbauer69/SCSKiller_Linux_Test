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
constexpr char kDebugEnv[] = "SCSKILLER_VK_DEBUG";

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
    PFN_vkCreateRenderPass CreateRenderPass = nullptr;
    PFN_vkDestroyRenderPass DestroyRenderPass = nullptr;
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
std::unordered_map<VkPhysicalDevice, VkInstance> g_physicalDeviceInstances;
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

struct RenderPassKey
{
    VkDevice device{};
    VkRenderPass renderPass{};

    bool operator==(const RenderPassKey& other) const
    {
        return device == other.device && renderPass == other.renderPass;
    }
};

struct RenderPassKeyHash
{
    size_t operator()(const RenderPassKey& key) const noexcept
    {
        const auto deviceBits = reinterpret_cast<uintptr_t>(key.device);
        const auto renderPassBits = static_cast<uint64_t>(key.renderPass);
        return static_cast<size_t>(
            (deviceBits >> 4) ^
            (renderPassBits + 0x243f6a8885a308d3ull + (deviceBits << 6) + (deviceBits >> 2)));
    }
};

std::unordered_map<RenderPassKey, uint64_t, RenderPassKeyHash> g_renderPassHashes;

std::atomic<uint64_t> g_sequence{1};
PFN_vkGetInstanceProcAddr g_nextInstanceProcAddr = nullptr;
PFN_GetPhysicalDeviceProcAddr g_nextPhysicalDeviceProcAddr = nullptr;

bool DebugEnabled()
{
    const char* value = std::getenv(kDebugEnv);
    return value && value[0] && std::strcmp(value, "0") != 0;
}

void Debug(const char* message)
{
    if (DebugEnabled())
        std::fprintf(stderr, "[SCSKiller Vulkan] %s\n", message);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
#endif
void ScskillerLayerLoaded()
{
    Debug("layer shared object initialized");
}


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

uint64_t HashRenderPassCreateInfo(const VkRenderPassCreateInfo* info)
{
    if (!info)
        return 0;

    uint64_t hash = HashCombine(1469598103934665603ull, info->flags);
    hash = HashCombine(hash, info->attachmentCount);

    for (uint32_t i = 0; i < info->attachmentCount; ++i)
    {
        const auto& attachment = info->pAttachments[i];
        hash = HashCombine(hash, attachment.flags);
        hash = HashCombine(hash, attachment.format);
        hash = HashCombine(hash, attachment.samples);
        hash = HashCombine(hash, attachment.loadOp);
        hash = HashCombine(hash, attachment.storeOp);
        hash = HashCombine(hash, attachment.stencilLoadOp);
        hash = HashCombine(hash, attachment.stencilStoreOp);
        hash = HashCombine(hash, attachment.initialLayout);
        hash = HashCombine(hash, attachment.finalLayout);
    }

    hash = HashCombine(hash, info->subpassCount);
    for (uint32_t i = 0; i < info->subpassCount; ++i)
    {
        const auto& subpass = info->pSubpasses[i];
        hash = HashCombine(hash, subpass.flags);
        hash = HashCombine(hash, subpass.pipelineBindPoint);

        hash = HashCombine(hash, subpass.inputAttachmentCount);
        for (uint32_t ref = 0; ref < subpass.inputAttachmentCount; ++ref)
        {
            hash = HashCombine(hash, subpass.pInputAttachments[ref].attachment);
            hash = HashCombine(hash, subpass.pInputAttachments[ref].layout);
        }

        hash = HashCombine(hash, subpass.colorAttachmentCount);
        for (uint32_t ref = 0; ref < subpass.colorAttachmentCount; ++ref)
        {
            hash = HashCombine(hash, subpass.pColorAttachments[ref].attachment);
            hash = HashCombine(hash, subpass.pColorAttachments[ref].layout);
        }

        hash = HashCombine(hash, subpass.pResolveAttachments ? subpass.colorAttachmentCount : 0);
        if (subpass.pResolveAttachments)
        {
            for (uint32_t ref = 0; ref < subpass.colorAttachmentCount; ++ref)
            {
                hash = HashCombine(hash, subpass.pResolveAttachments[ref].attachment);
                hash = HashCombine(hash, subpass.pResolveAttachments[ref].layout);
            }
        }

        hash = HashCombine(hash, subpass.pDepthStencilAttachment ? 1u : 0u);
        if (subpass.pDepthStencilAttachment)
        {
            hash = HashCombine(hash, subpass.pDepthStencilAttachment->attachment);
            hash = HashCombine(hash, subpass.pDepthStencilAttachment->layout);
        }

        hash = HashCombine(hash, subpass.preserveAttachmentCount);
        for (uint32_t ref = 0; ref < subpass.preserveAttachmentCount; ++ref)
            hash = HashCombine(hash, subpass.pPreserveAttachments[ref]);
    }

    hash = HashCombine(hash, info->dependencyCount);
    for (uint32_t i = 0; i < info->dependencyCount; ++i)
    {
        const auto& dependency = info->pDependencies[i];
        hash = HashCombine(hash, dependency.srcSubpass);
        hash = HashCombine(hash, dependency.dstSubpass);
        hash = HashCombine(hash, dependency.srcStageMask);
        hash = HashCombine(hash, dependency.dstStageMask);
        hash = HashCombine(hash, dependency.srcAccessMask);
        hash = HashCombine(hash, dependency.dstAccessMask);
        hash = HashCombine(hash, dependency.dependencyFlags);
    }

    return hash;
}

bool RenderPassReplayCompatible(const VkRenderPassCreateInfo* info)
{
    return info && info->pNext == nullptr &&
           std::all_of(info->pAttachments, info->pAttachments + info->attachmentCount,
               [](const VkAttachmentDescription& attachment)
               {
                   return attachment.pNext == nullptr;
               });
}

void RecordRenderPassCreate(const VkRenderPassCreateInfo* info, uint64_t sequence, uint64_t hash)
{
    if (!RecordingEnabled() || !info)
        return;

    const char* path = RecordingPath();
    if (!path)
        return;

    if (std::FILE* file = std::fopen(path, "ab"))
    {
        std::fprintf(file,
            "{\"schema\":3,\"event\":\"render_pass_create\",\"sequence\":%llu,\"hash\":\"%016llx\",\"flags\":%u,\"replay_compatible\":%s,\"attachments\":[",
            static_cast<unsigned long long>(sequence),
            static_cast<unsigned long long>(hash),
            info->flags,
            RenderPassReplayCompatible(info) ? "true" : "false");

        for (uint32_t i = 0; i < info->attachmentCount; ++i)
        {
            if (i) std::fputc(',', file);
            const auto& attachment = info->pAttachments[i];
            std::fprintf(file,
                "{\"flags\":%u,\"format\":%d,\"samples\":%u,\"load_op\":%u,\"store_op\":%u,\"stencil_load_op\":%u,\"stencil_store_op\":%u,\"initial_layout\":%u,\"final_layout\":%u}",
                attachment.flags,
                attachment.format,
                attachment.samples,
                attachment.loadOp,
                attachment.storeOp,
                attachment.stencilLoadOp,
                attachment.stencilStoreOp,
                attachment.initialLayout,
                attachment.finalLayout);
        }

        std::fputs("],\"subpasses\":[", file);
        for (uint32_t i = 0; i < info->subpassCount; ++i)
        {
            if (i) std::fputc(',', file);
            const auto& subpass = info->pSubpasses[i];

            std::fprintf(file,
                "{\"flags\":%u,\"pipeline_bind_point\":%u,\"input_attachments\":[",
                subpass.flags, subpass.pipelineBindPoint);

            for (uint32_t ref = 0; ref < subpass.inputAttachmentCount; ++ref)
            {
                if (ref) std::fputc(',', file);
                std::fprintf(file,
                    "{\"attachment\":%u,\"layout\":%u}",
                    subpass.pInputAttachments[ref].attachment,
                    subpass.pInputAttachments[ref].layout);
            }

            std::fputs("],\"color_attachments\":[", file);
            for (uint32_t ref = 0; ref < subpass.colorAttachmentCount; ++ref)
            {
                if (ref) std::fputc(',', file);
                std::fprintf(file,
                    "{\"attachment\":%u,\"layout\":%u}",
                    subpass.pColorAttachments[ref].attachment,
                    subpass.pColorAttachments[ref].layout);
            }

            std::fputs("],\"resolve_attachments\":[", file);
            if (subpass.pResolveAttachments)
            {
                for (uint32_t ref = 0; ref < subpass.colorAttachmentCount; ++ref)
                {
                    if (ref) std::fputc(',', file);
                    std::fprintf(file,
                        "{\"attachment\":%u,\"layout\":%u}",
                        subpass.pResolveAttachments[ref].attachment,
                        subpass.pResolveAttachments[ref].layout);
                }
            }

            std::fputs("],\"depth_stencil\":", file);
            if (subpass.pDepthStencilAttachment)
            {
                std::fprintf(file,
                    "{\"attachment\":%u,\"layout\":%u}",
                    subpass.pDepthStencilAttachment->attachment,
                    subpass.pDepthStencilAttachment->layout);
            }
            else
            {
                std::fputs("null", file);
            }

            std::fputs(",\"preserve_attachments\":[", file);
            for (uint32_t ref = 0; ref < subpass.preserveAttachmentCount; ++ref)
            {
                if (ref) std::fputc(',', file);
                std::fprintf(file, "%u", subpass.pPreserveAttachments[ref]);
            }

            std::fputs("]}", file);
        }

        std::fputs("],\"dependencies\":[", file);
        for (uint32_t i = 0; i < info->dependencyCount; ++i)
        {
            if (i) std::fputc(',', file);
            const auto& dependency = info->pDependencies[i];
            std::fprintf(file,
                "{\"src_subpass\":%u,\"dst_subpass\":%u,\"src_stage_mask\":%u,\"dst_stage_mask\":%u,\"src_access_mask\":%u,\"dst_access_mask\":%u,\"dependency_flags\":%u}",
                dependency.srcSubpass,
                dependency.dstSubpass,
                dependency.srcStageMask,
                dependency.dstStageMask,
                dependency.srcAccessMask,
                dependency.dstAccessMask,
                dependency.dependencyFlags);
        }

        std::fputs("]}\n", file);
        std::fclose(file);
    }
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

std::string Base64(const uint8_t* data, size_t size);

void RecordSpecialization(std::FILE* file, const VkSpecializationInfo* info)
{
    if (!info)
    {
        std::fprintf(file, "null");
        return;
    }

    std::fprintf(file, "{\"hash\":\"%016llx\",\"data_size\":%zu,\"map_entries\":[",
                 static_cast<unsigned long long>(HashSpecializationInfo(info)),
                 info->dataSize);
    for (uint32_t i = 0; i < info->mapEntryCount; ++i)
    {
        if (i) std::fputc(',', file);
        const auto& entry = info->pMapEntries[i];
        std::fprintf(file,
            "{\"constant_id\":%u,\"offset\":%zu,\"size\":%zu}",
            entry.constantID, static_cast<size_t>(entry.offset), static_cast<size_t>(entry.size));
    }
    std::fprintf(file, "],\"data_base64\":\"%s\"}",
        info->pData && info->dataSize
            ? Base64(static_cast<const uint8_t*>(info->pData), info->dataSize).c_str()
            : "");
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

std::string JsonEscape(std::string_view value)
{
    std::string out;
    out.reserve(value.size() + 8);
    for (const char c : value)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(c); break;
        }
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

    const auto writeFloat = [](std::FILE* file, float value)
    {
        std::fprintf(file, "%.9g", static_cast<double>(value));
    };

    if (std::FILE* file = std::fopen(path, "ab"))
    {
        std::fprintf(file,
            "{\"schema\":3,\"event\":\"graphics_pipeline_state\",\"sequence\":%llu,\"count\":%u,\"pipelines\":[",
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

                std::fprintf(file,
                    "{\"stage\":\"%s\",\"stage_flags\":%u,\"module_hash\":\"%016llx\",\"entry_point\":\"%s\",\"specialization\":",
                    ShaderStageName(state.stage),
                    state.flags,
                    static_cast<unsigned long long>(shaderHash),
                    JsonEscape(state.pName ? state.pName : "main").c_str());
                RecordSpecialization(file, state.pSpecializationInfo);
                std::fputc('}', file);
            }

            std::fprintf(file,
                "],\"flags\":%u,\"subpass\":%u,\"base_pipeline_index\":%d",
                info.flags, info.subpass, info.basePipelineIndex);

            const bool hasLegacyRenderPass = info.renderPass != VK_NULL_HANDLE;
            std::fprintf(file, ",\"legacy_render_pass\":%s", hasLegacyRenderPass ? "true" : "false");

            const auto* node = reinterpret_cast<const VkBaseInStructure*>(info.pNext);
            const VkPipelineRenderingCreateInfo* rendering = nullptr;
            while (node)
            {
                if (node->sType == VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO)
                {
                    rendering = reinterpret_cast<const VkPipelineRenderingCreateInfo*>(node);
                    break;
                }
                node = node->pNext;
            }

            if (rendering)
            {
                std::fprintf(file,
                    ",\"dynamic_rendering\":{\"view_mask\":%u,\"color_formats\":[",
                    rendering->viewMask);
                for (uint32_t format = 0; format < rendering->colorAttachmentCount; ++format)
                {
                    if (format) std::fputc(',', file);
                    std::fprintf(file, "%d", rendering->pColorAttachmentFormats[format]);
                }
                std::fprintf(file,
                    "],\"depth_format\":%d,\"stencil_format\":%d}",
                    rendering->depthAttachmentFormat,
                    rendering->stencilAttachmentFormat);
            }
            else
            {
                std::fputs(",\"dynamic_rendering\":null", file);
            }

            if (info.pVertexInputState)
            {
                const auto& state = *info.pVertexInputState;
                std::fprintf(file, ",\"vertex_input\":{\"flags\":%u,\"bindings\":[", state.flags);
                for (uint32_t binding = 0; binding < state.vertexBindingDescriptionCount; ++binding)
                {
                    if (binding) std::fputc(',', file);
                    const auto& description = state.pVertexBindingDescriptions[binding];
                    std::fprintf(file,
                        "{\"binding\":%u,\"stride\":%u,\"input_rate\":%u}",
                        description.binding, description.stride, description.inputRate);
                }
                std::fputs("],\"attributes\":[", file);
                for (uint32_t attribute = 0; attribute < state.vertexAttributeDescriptionCount; ++attribute)
                {
                    if (attribute) std::fputc(',', file);
                    const auto& description = state.pVertexAttributeDescriptions[attribute];
                    std::fprintf(file,
                        "{\"location\":%u,\"binding\":%u,\"format\":%d,\"offset\":%u}",
                        description.location, description.binding, description.format, description.offset);
                }
                std::fprintf(file, "],\"pnext_present\":%s}",
                             state.pNext ? "true" : "false");
            }
            else
            {
                std::fputs(",\"vertex_input\":null", file);
            }

            if (info.pInputAssemblyState)
            {
                const auto& state = *info.pInputAssemblyState;
                std::fprintf(file,
                    ",\"input_assembly\":{\"flags\":%u,\"topology\":%u,\"primitive_restart\":%s,\"pnext_present\":%s}",
                    state.flags, state.topology,
                    state.primitiveRestartEnable ? "true" : "false",
                    state.pNext ? "true" : "false");
            }
            else
            {
                std::fputs(",\"input_assembly\":null", file);
            }

            if (info.pTessellationState)
            {
                const auto& state = *info.pTessellationState;
                std::fprintf(file,
                    ",\"tessellation\":{\"flags\":%u,\"patch_control_points\":%u,\"pnext_present\":%s}",
                    state.flags, state.patchControlPoints, state.pNext ? "true" : "false");
            }
            else
            {
                std::fputs(",\"tessellation\":null", file);
            }

            if (info.pViewportState)
            {
                const auto& state = *info.pViewportState;
                std::fprintf(file,
                    ",\"viewport_state\":{\"flags\":%u,\"viewport_count\":%u,\"scissor_count\":%u,\"viewports\":[",
                    state.flags, state.viewportCount, state.scissorCount);
                if (state.pViewports)
                {
                    for (uint32_t viewport = 0; viewport < state.viewportCount; ++viewport)
                    {
                        if (viewport) std::fputc(',', file);
                        const auto& value = state.pViewports[viewport];
                        std::fputs("{\"x\":", file); writeFloat(file, value.x);
                        std::fputs(",\"y\":", file); writeFloat(file, value.y);
                        std::fputs(",\"width\":", file); writeFloat(file, value.width);
                        std::fputs(",\"height\":", file); writeFloat(file, value.height);
                        std::fputs(",\"min_depth\":", file); writeFloat(file, value.minDepth);
                        std::fputs(",\"max_depth\":", file); writeFloat(file, value.maxDepth);
                        std::fputc('}', file);
                    }
                }
                std::fputs("],\"scissors\":[", file);
                if (state.pScissors)
                {
                    for (uint32_t scissor = 0; scissor < state.scissorCount; ++scissor)
                    {
                        if (scissor) std::fputc(',', file);
                        const auto& value = state.pScissors[scissor];
                        std::fprintf(file,
                            "{\"offset_x\":%d,\"offset_y\":%d,\"extent_width\":%u,\"extent_height\":%u}",
                            value.offset.x, value.offset.y, value.extent.width, value.extent.height);
                    }
                }
                std::fprintf(file, "],\"pnext_present\":%s}", state.pNext ? "true" : "false");
            }
            else
            {
                std::fputs(",\"viewport_state\":null", file);
            }

            if (info.pRasterizationState)
            {
                const auto& state = *info.pRasterizationState;
                std::fprintf(file,
                    ",\"rasterization\":{\"flags\":%u,\"depth_clamp\":%s,\"rasterizer_discard\":%s,\"polygon_mode\":%u,\"cull_mode\":%u,\"front_face\":%u,\"depth_bias_enable\":%s,\"depth_bias_constant\":",
                    state.flags,
                    state.depthClampEnable ? "true" : "false",
                    state.rasterizerDiscardEnable ? "true" : "false",
                    state.polygonMode, state.cullMode, state.frontFace,
                    state.depthBiasEnable ? "true" : "false");
                writeFloat(file, state.depthBiasConstantFactor);
                std::fputs(",\"depth_bias_clamp\":", file); writeFloat(file, state.depthBiasClamp);
                std::fputs(",\"depth_bias_slope\":", file); writeFloat(file, state.depthBiasSlopeFactor);
                std::fputs(",\"line_width\":", file); writeFloat(file, state.lineWidth);
                std::fprintf(file, ",\"pnext_present\":%s}", state.pNext ? "true" : "false");
            }
            else
            {
                std::fputs(",\"rasterization\":null", file);
            }

            if (info.pMultisampleState)
            {
                const auto& state = *info.pMultisampleState;
                std::fprintf(file,
                    ",\"multisample\":{\"flags\":%u,\"rasterization_samples\":%u,\"sample_shading\":%s,\"min_sample_shading\":",
                    state.flags,
                    state.rasterizationSamples,
                    state.sampleShadingEnable ? "true" : "false");
                writeFloat(file, state.minSampleShading);
                std::fprintf(file,
                    ",\"alpha_to_coverage\":%s,\"alpha_to_one\":%s,\"sample_mask_word_count\":%u,\"sample_mask\":[",
                    state.alphaToCoverageEnable ? "true" : "false",
                    state.alphaToOneEnable ? "true" : "false",
                    state.pSampleMask ? ((static_cast<uint32_t>(state.rasterizationSamples) + 31u) / 32u) : 0u);
                if (state.pSampleMask)
                {
                    const uint32_t wordCount =
                        (static_cast<uint32_t>(state.rasterizationSamples) + 31u) / 32u;
                    for (uint32_t word = 0; word < wordCount; ++word)
                    {
                        if (word) std::fputc(',', file);
                        std::fprintf(file, "%u", state.pSampleMask[word]);
                    }
                }
                std::fprintf(file, "],\"pnext_present\":%s}", state.pNext ? "true" : "false");
            }
            else
            {
                std::fputs(",\"multisample\":null", file);
            }

            if (info.pDepthStencilState)
            {
                const auto& state = *info.pDepthStencilState;
                const auto writeStencil = [file](const VkStencilOpState& stencil)
                {
                    std::fprintf(file,
                        "{\"fail_op\":%u,\"pass_op\":%u,\"depth_fail_op\":%u,\"compare_op\":%u,\"compare_mask\":%u,\"write_mask\":%u,\"reference\":%u}",
                        stencil.failOp, stencil.passOp, stencil.depthFailOp, stencil.compareOp,
                        stencil.compareMask, stencil.writeMask, stencil.reference);
                };
                std::fprintf(file,
                    ",\"depth_stencil\":{\"flags\":%u,\"depth_test\":%s,\"depth_write\":%s,\"depth_compare_op\":%u,\"depth_bounds\":%s,\"min_depth_bounds\":",
                    state.flags,
                    state.depthTestEnable ? "true" : "false",
                    state.depthWriteEnable ? "true" : "false",
                    state.depthCompareOp,
                    state.depthBoundsTestEnable ? "true" : "false");
                writeFloat(file, state.minDepthBounds);
                std::fputs(",\"max_depth_bounds\":", file); writeFloat(file, state.maxDepthBounds);
                std::fprintf(file,
                    ",\"stencil_test\":%s,\"front\":", state.stencilTestEnable ? "true" : "false");
                writeStencil(state.front);
                std::fputs(",\"back\":", file);
                writeStencil(state.back);
                std::fprintf(file, ",\"pnext_present\":%s}", state.pNext ? "true" : "false");
            }
            else
            {
                std::fputs(",\"depth_stencil\":null", file);
            }

            if (info.pColorBlendState)
            {
                const auto& state = *info.pColorBlendState;
                std::fprintf(file,
                    ",\"color_blend\":{\"flags\":%u,\"logic_op_enable\":%s,\"logic_op\":%u,\"blend_constants\":[",
                    state.flags, state.logicOpEnable ? "true" : "false", state.logicOp);
                for (size_t constant = 0; constant < 4; ++constant)
                {
                    if (constant) std::fputc(',', file);
                    writeFloat(file, state.blendConstants[constant]);
                }
                std::fputs("],\"attachments\":[", file);
                if (state.pAttachments)
                {
                    for (uint32_t attachment = 0; attachment < state.attachmentCount; ++attachment)
                    {
                        if (attachment) std::fputc(',', file);
                        const auto& value = state.pAttachments[attachment];
                    std::fprintf(file,
                        "{\"blend_enable\":%s,\"src_color_factor\":%u,\"dst_color_factor\":%u,\"color_op\":%u,\"src_alpha_factor\":%u,\"dst_alpha_factor\":%u,\"alpha_op\":%u,\"color_write_mask\":%u}",
                        value.blendEnable ? "true" : "false",
                        value.srcColorBlendFactor, value.dstColorBlendFactor, value.colorBlendOp,
                        value.srcAlphaBlendFactor, value.dstAlphaBlendFactor, value.alphaBlendOp,
                            value.colorWriteMask);
                    }
                }
                std::fprintf(file, "],\"pnext_present\":%s}", state.pNext ? "true" : "false");
            }
            else
            {
                std::fputs(",\"color_blend\":null", file);
            }

            if (info.pDynamicState)
            {
                const auto& state = *info.pDynamicState;
                std::fprintf(file,
                    ",\"dynamic_state\":{\"flags\":%u,\"states\":[",
                    state.flags);
                for (uint32_t dynamic = 0; dynamic < state.dynamicStateCount; ++dynamic)
                {
                    if (dynamic) std::fputc(',', file);
                    std::fprintf(file, "%u", state.pDynamicStates[dynamic]);
                }
                std::fprintf(file, "],\"pnext_present\":%s}", state.pNext ? "true" : "false");
            }
            else
            {
                std::fputs(",\"dynamic_state\":null", file);
            }

            std::fputc('}', file);
        }

        std::fputs("]}\n", file);
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
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_layerGetPhysicalDeviceProcAddr(VkInstance, const char*);

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* versionStruct)
{
    Debug("vkNegotiateLoaderLayerInterfaceVersion");
    if (!versionStruct)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (versionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (versionStruct->loaderLayerInterfaceVersion < 2)
        return VK_ERROR_INITIALIZATION_FAILED;

    versionStruct->loaderLayerInterfaceVersion = 2;
    versionStruct->pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    versionStruct->pfnGetDeviceProcAddr = vkGetDeviceProcAddr;
    versionStruct->pfnGetPhysicalDeviceProcAddr = vk_layerGetPhysicalDeviceProcAddr;
    Debug("loader interface negotiated");
    return VK_SUCCESS;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceExtensionProperties(const char* layerName,
                                       uint32_t* propertyCount,
                                       VkExtensionProperties* properties)
{
    (void)properties;

    if (!propertyCount)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (!layerName || std::strcmp(layerName, kLayerName) != 0)
        return VK_ERROR_LAYER_NOT_PRESENT;

    *propertyCount = 0;
    return VK_SUCCESS;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice,
                                     const char* layerName,
                                     uint32_t* propertyCount,
                                     VkExtensionProperties* properties)
{
    if (!propertyCount)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (layerName && std::strcmp(layerName, kLayerName) == 0)
    {
        *propertyCount = 0;
        return VK_SUCCESS;
    }

    VkInstance instance = VK_NULL_HANDLE;
    if (physicalDevice != VK_NULL_HANDLE)
    {
        std::lock_guard lock(g_mutex);
        auto it = g_physicalDeviceInstances.find(physicalDevice);
        if (it != g_physicalDeviceInstances.end())
            instance = it->second;
    }

    if (!g_nextInstanceProcAddr || instance == VK_NULL_HANDLE)
        return VK_ERROR_INITIALIZATION_FAILED;

    auto enumerateNext = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
        g_nextInstanceProcAddr(instance, "vkEnumerateDeviceExtensionProperties"));
    if (!enumerateNext)
        return VK_ERROR_INITIALIZATION_FAILED;

    return enumerateNext(physicalDevice, layerName, propertyCount, properties);
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
    Debug("vkCreateInstance: entered");
    auto* linkInfo = FindInstanceLinkInfo(createInfo);
    if (!linkInfo || !linkInfo->u.pLayerInfo || !instance)
        return VK_ERROR_INITIALIZATION_FAILED;

    auto next = linkInfo->u.pLayerInfo;
    linkInfo->u.pLayerInfo = next->pNext;

    g_nextInstanceProcAddr = next->pfnNextGetInstanceProcAddr;
    g_nextPhysicalDeviceProcAddr = next->pfnNextGetPhysicalDeviceProcAddr;
    Debug("vkCreateInstance: calling next layer/driver");

    auto createNext = reinterpret_cast<PFN_vkCreateInstance>(
        next->pfnNextGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));

    if (!createNext)
        return VK_ERROR_INITIALIZATION_FAILED;

    const VkResult result = createNext(createInfo, allocator, instance);
    if (result == VK_SUCCESS)
        Debug("vkCreateInstance: success");
    else
        Debug("vkCreateInstance: failed");
    return result;
}

extern "C" VKAPI_ATTR void VKAPI_CALL
vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator)
{
    Debug("vkDestroyInstance");

    if (g_nextInstanceProcAddr)
    {
        auto destroyNext = reinterpret_cast<PFN_vkDestroyInstance>(
            g_nextInstanceProcAddr(instance, "vkDestroyInstance"));
        if (destroyNext)
            destroyNext(instance, allocator);
    }

    for (auto it = g_physicalDeviceInstances.begin(); it != g_physicalDeviceInstances.end();)
    {
        if (it->second == instance)
            it = g_physicalDeviceInstances.erase(it);
        else
            ++it;
    }
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkEnumeratePhysicalDevices(VkInstance instance,
                           uint32_t* deviceCount,
                           VkPhysicalDevice* physicalDevices)
{
    if (!g_nextInstanceProcAddr)
        return VK_ERROR_INITIALIZATION_FAILED;

    auto enumerateNext = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        g_nextInstanceProcAddr(instance, "vkEnumeratePhysicalDevices"));
    if (!enumerateNext)
        return VK_ERROR_INITIALIZATION_FAILED;

    const VkResult result = enumerateNext(instance, deviceCount, physicalDevices);
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) &&
        deviceCount && physicalDevices)
    {
        for (uint32_t i = 0; i < *deviceCount; ++i)
            g_physicalDeviceInstances[physicalDevices[i]] = instance;
    }

    return result;
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

    VkInstance instance = VK_NULL_HANDLE;
    {
        auto it = g_physicalDeviceInstances.find(physicalDevice);
        if (it != g_physicalDeviceInstances.end())
            instance = it->second;
    }

    if (instance == VK_NULL_HANDLE)
    {
        Debug("vkCreateDevice: physical device has no known parent instance");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    Debug("vkCreateDevice: resolving next vkCreateDevice with parent instance");
    auto createNext = reinterpret_cast<PFN_vkCreateDevice>(
        next->pfnNextGetInstanceProcAddr(instance, "vkCreateDevice"));

    if (!createNext)
    {
        Debug("vkCreateDevice: next vkCreateDevice was not found");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = createNext(physicalDevice, createInfo, allocator, device);
    if (result != VK_SUCCESS)
        return result;

    DeviceDispatch dispatch{};
    dispatch.GetDeviceProcAddr = next->pfnNextGetDeviceProcAddr;

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
        if (result == VK_SUCCESS)
        {
            RecordShader("shader_module_create", sequence, hash, wordCount);
            RecordShaderCode(sequence, hash, createInfo->pCode, wordCount);
        }
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
                    std::fprintf(file,
                        "{\"layout_hash\":\"%016llx\",\"module_hash\":\"%016llx\",\"stage_flags\":%u,\"stage\":\"%s\",\"entry_point\":\"%s\",\"specialization\":",
                        static_cast<unsigned long long>(layoutHash),
                        static_cast<unsigned long long>(shaderHash),
                        info.stage.flags,
                        ShaderStageName(info.stage.stage),
                        info.stage.pName ? info.stage.pName : "main");
                    RecordSpecialization(file, info.stage.pSpecializationInfo);
                    std::fprintf(file,
                        ",\"flags\":%u}",
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

    if (DebugEnabled() &&
        (std::strcmp(name, "vkCreateInstance") == 0 ||
         std::strcmp(name, "vkGetInstanceProcAddr") == 0 ||
         std::strcmp(name, "vkEnumeratePhysicalDevices") == 0 ||
         std::strcmp(name, "vkCreateDevice") == 0))
    {
        std::fprintf(stderr, "[SCSKiller Vulkan] GIPA %s\n", name);
    }

    if (std::strcmp(name, "vkNegotiateLoaderLayerInterfaceVersion") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkNegotiateLoaderLayerInterfaceVersion);
    if (std::strcmp(name, "vkGetInstanceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetInstanceProcAddr);
    if (std::strcmp(name, "vkCreateInstance") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateInstance);
    if (std::strcmp(name, "vkDestroyInstance") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyInstance);
    if (std::strcmp(name, "vkEnumeratePhysicalDevices") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkEnumeratePhysicalDevices);
    if (std::strcmp(name, "vkCreateDevice") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateDevice);
    if (std::strcmp(name, "vkEnumerateInstanceLayerProperties") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceLayerProperties);
    if (std::strcmp(name, "vkEnumerateInstanceExtensionProperties") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceExtensionProperties);
    if (std::strcmp(name, "vkEnumerateDeviceExtensionProperties") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateDeviceExtensionProperties);
    if (std::strcmp(name, "vk_layerGetPhysicalDeviceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vk_layerGetPhysicalDeviceProcAddr);

    return g_nextInstanceProcAddr ? g_nextInstanceProcAddr(instance, name) : nullptr;
}


extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_layerGetPhysicalDeviceProcAddr(VkInstance instance, const char* name)
{
    if (!name)
        return nullptr;

    if (g_nextPhysicalDeviceProcAddr)
        return g_nextPhysicalDeviceProcAddr(instance, name);

    return g_nextInstanceProcAddr ? g_nextInstanceProcAddr(instance, name) : nullptr;
}
