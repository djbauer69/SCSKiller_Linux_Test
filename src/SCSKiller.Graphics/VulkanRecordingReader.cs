using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace SCSKiller.Graphics;

public sealed record VulkanCapturedDevice(
    string Name,
    uint VendorId,
    uint DeviceId,
    uint DriverVersion,
    uint ApiVersion,
    string PipelineCacheUuid);

public sealed record VulkanRecordingReadResult(
    string RecordingPath,
    IReadOnlyList<PipelineDescription> Pipelines,
    IReadOnlyList<ShaderArtifact> Shaders,
    IReadOnlyDictionary<string, int> EventCounts,
    VulkanCapturedDevice? Device,
    long CacheReplays,
    long CacheReplaySkips,
    IReadOnlyList<string> MissingShaderHashes);

/// <summary>
/// Reads Vulkan JSONL captures into the cross-platform graphics contracts.
/// Raw Vulkan state is retained as JSON in metadata dictionaries; unknown state
/// is marked non-replayable instead of being guessed.
/// </summary>
public static class VulkanRecordingReader
{
    private sealed record CapturedPipeline(
        string EventName,
        long Sequence,
        int IndexInEvent,
        string Kind,
        JsonElement State);

    public static VulkanRecordingReadResult Read(
        string recordingPath,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(recordingPath);
        if (!File.Exists(recordingPath))
            throw new FileNotFoundException("Vulkan recording was not found.", recordingPath);

        var shaderCode = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);
        var descriptorLayouts = new Dictionary<string, JsonElement>(StringComparer.OrdinalIgnoreCase);
        var pipelineLayouts = new Dictionary<string, JsonElement>(StringComparer.OrdinalIgnoreCase);
        var renderPasses = new Dictionary<string, JsonElement>(StringComparer.OrdinalIgnoreCase);
        var capturedPipelines = new List<CapturedPipeline>();
        var eventCounts = new SortedDictionary<string, int>(StringComparer.Ordinal);
        var missingShaderHashes = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        VulkanCapturedDevice? device = null;
        long cacheReplays = 0;
        long cacheReplaySkips = 0;
        var lineNumber = 0;

        foreach (var line in File.ReadLines(recordingPath))
        {
            cancellationToken.ThrowIfCancellationRequested();
            lineNumber++;
            if (string.IsNullOrWhiteSpace(line))
                continue;

            JsonDocument document;
            try
            {
                document = JsonDocument.Parse(line);
            }
            catch (JsonException exception)
            {
                throw new InvalidDataException(
                    $"Invalid JSON in Vulkan recording at line {lineNumber}: {exception.Message}",
                    exception);
            }

            using (document)
            {
                var root = document.RootElement;
                var eventName = GetString(root, "event");
                if (string.IsNullOrWhiteSpace(eventName))
                    throw new InvalidDataException(
                        $"Vulkan recording line {lineNumber} is missing an event name.");

                eventCounts[eventName] = eventCounts.GetValueOrDefault(eventName) + 1;

                switch (eventName)
                {
                    case "shader_module_code":
                    {
                        var hash = NormalizeHash(GetString(root, "hash"));
                        var encoded = GetString(root, "code_base64");
                        if (hash is null || encoded is null)
                            throw new InvalidDataException(
                                $"Shader code event at line {lineNumber} is missing hash or code_base64.");

                        try
                        {
                            shaderCode[hash] = Convert.FromBase64String(encoded);
                        }
                        catch (FormatException exception)
                        {
                            throw new InvalidDataException(
                                $"Shader code event at line {lineNumber} has invalid Base64 data.",
                                exception);
                        }
                        break;
                    }
                    case "descriptor_set_layout_create":
                    {
                        var hash = NormalizeHash(GetString(root, "hash"));
                        if (hash is not null)
                            descriptorLayouts[hash] = root.Clone();
                        break;
                    }
                    case "pipeline_layout_create":
                    {
                        var hash = NormalizeHash(GetString(root, "hash"));
                        if (hash is not null)
                            pipelineLayouts[hash] = root.Clone();
                        break;
                    }
                    case "render_pass_create":
                    {
                        var hash = NormalizeHash(GetString(root, "hash"));
                        if (hash is not null)
                            renderPasses[hash] = root.Clone();
                        break;
                    }
                    case "compute_pipeline_state":
                    case "graphics_pipeline_state":
                    {
                        if (!root.TryGetProperty("pipelines", out var pipelines) ||
                            pipelines.ValueKind != JsonValueKind.Array)
                        {
                            throw new InvalidDataException(
                                $"{eventName} event at line {lineNumber} is missing its pipelines array.");
                        }

                        var kind = eventName == "compute_pipeline_state" ? "compute" : "graphics";
                        var sequence = GetInt64(root, "sequence") ?? 0;
                        var indexInEvent = 0;
                        foreach (var pipeline in pipelines.EnumerateArray())
                        {
                            capturedPipelines.Add(new CapturedPipeline(
                                eventName,
                                sequence,
                                indexInEvent++,
                                kind,
                                pipeline.Clone()));
                        }
                        break;
                    }
                    case "physical_device_identity":
                    {
                        var name = GetString(root, "device_name") ?? "Unknown Vulkan device";
                        device = new VulkanCapturedDevice(
                            name,
                            GetUInt32(root, "vendor_id") ?? 0,
                            GetUInt32(root, "device_id") ?? 0,
                            GetUInt32(root, "driver_version") ?? 0,
                            GetUInt32(root, "api_version") ?? 0,
                            GetString(root, "pipeline_cache_uuid") ?? string.Empty);
                        break;
                    }
                    case "pipeline_cache_replay":
                    case "pipeline_cache_merge":
                        cacheReplays += ReadCount(root);
                        break;
                    case "pipeline_cache_replay_skipped":
                    case "pipeline_cache_merge_skipped":
                        cacheReplaySkips += ReadCount(root);
                        break;
                }
            }
        }

        var descriptions = new List<PipelineDescription>(capturedPipelines.Count);
        var allShaders = new List<ShaderArtifact>();
        var seenArtifacts = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        foreach (var captured in capturedPipelines)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var state = captured.State;
            var shaderArtifacts = new List<ShaderArtifact>();
            var shaderHashesForPipeline = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var pipelineMissingHashes = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

            if (captured.Kind == "compute")
            {
                AddShaderUse(
                    state,
                    "module_hash",
                    GetString(state, "stage") ?? "compute",
                    GetString(state, "entry_point") ?? "main",
                    GetPropertyRaw(state, "specialization"),
                    shaderCode,
                    shaderArtifacts,
                    allShaders,
                    seenArtifacts,
                    missingShaderHashes,
                    pipelineMissingHashes,
                    shaderHashesForPipeline);
            }
            else if (state.TryGetProperty("stages", out var stages) &&
                     stages.ValueKind == JsonValueKind.Array)
            {
                foreach (var stage in stages.EnumerateArray())
                {
                    AddShaderUse(
                        stage,
                        "module_hash",
                        GetString(stage, "stage") ?? "unknown",
                        GetString(stage, "entry_point") ?? "main",
                        GetPropertyRaw(stage, "specialization"),
                        shaderCode,
                        shaderArtifacts,
                        allShaders,
                        seenArtifacts,
                        missingShaderHashes,
                        pipelineMissingHashes,
                        shaderHashesForPipeline);
                }
            }

            var layoutHash = NormalizeHash(GetString(state, "layout_hash"));
            var renderPassHash = NormalizeHash(GetString(state, "render_pass_hash"));
            var isDynamicRendering = GetObjectProperty(state, "dynamic_rendering") is not null;
            var incompatibilityReasons = new SortedSet<string>(StringComparer.Ordinal);

            if (layoutHash is null)
                incompatibilityReasons.Add("missing-pipeline-layout-hash");
            else if (!IsPipelineLayoutCompatible(layoutHash, pipelineLayouts, descriptorLayouts))
                incompatibilityReasons.Add("incompatible-pipeline-layout");

            if (pipelineMissingHashes.Count > 0)
                incompatibilityReasons.Add("missing-shader-code");

            if (!GetBoolean(state, "replay_compatible", true))
                incompatibilityReasons.Add("unsupported-pipeline-state");

            if (captured.Kind == "graphics")
            {
                if (HasUnsupportedPNext(state))
                    incompatibilityReasons.Add("unsupported-pipeline-pnext");

                if ((GetInt64(state, "base_pipeline_index") ?? -1) >= 0)
                    incompatibilityReasons.Add("pipeline-derivative-requires-creation-batch");

                if (!isDynamicRendering)
                {
                    if (!GetBoolean(state, "legacy_render_pass", true))
                        incompatibilityReasons.Add("unsupported-render-pass-mode");

                    if (renderPassHash is null ||
                        !renderPasses.TryGetValue(renderPassHash, out var renderPass) ||
                        !GetBoolean(renderPass, "replay_compatible", false))
                    {
                        incompatibilityReasons.Add("missing-or-unsupported-render-pass");
                    }
                }
            }
            else
            {
                if (GetBoolean(state, "stage_pnext_present", false))
                    incompatibilityReasons.Add("unsupported-shader-stage-pnext");

                if (GetBoolean(state, "pipeline_pnext_present", false) &&
                    !GetBoolean(state, "pipeline_pnext_compatible", false))
                    incompatibilityReasons.Add("unsupported-pipeline-pnext");
            }

            var compatible = incompatibilityReasons.Count == 0;

            var fixedState = BuildFixedFunctionState(captured.Kind, state, isDynamicRendering);
            var resourceInterface = BuildResourceInterface(
                layoutHash,
                renderPassHash,
                isDynamicRendering,
                pipelineLayouts,
                descriptorLayouts,
                renderPasses);

            var rawPipelineState = state.GetRawText();
            var pipelineId = Convert.ToHexString(
                SHA256.HashData(Encoding.UTF8.GetBytes(rawPipelineState)))
                .ToLowerInvariant()[..16];

            var backendMetadata = new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["graphics_api"] = "Vulkan",
                ["pipeline_kind"] = captured.Kind,
                ["pipeline_id"] = pipelineId,
                ["source_event"] = captured.EventName,
                ["capture_sequence"] = captured.Sequence.ToString(System.Globalization.CultureInfo.InvariantCulture),
                ["capture_pipeline_index"] = captured.IndexInEvent.ToString(System.Globalization.CultureInfo.InvariantCulture),
                ["layout_hash"] = layoutHash ?? string.Empty,
                ["render_pass_hash"] = renderPassHash ?? string.Empty,
                ["dynamic_rendering"] = isDynamicRendering ? "true" : "false",
                ["replay_compatible"] = compatible ? "true" : "false",
                ["missing_shader_hashes"] = string.Join(",", pipelineMissingHashes.Order(StringComparer.OrdinalIgnoreCase)),
                ["replay_incompatibility_reasons"] = string.Join(";", incompatibilityReasons),
                ["raw_pipeline_state_json"] = rawPipelineState
            };

            descriptions.Add(new PipelineDescription(
                shaderArtifacts,
                fixedState,
                resourceInterface,
                backendMetadata));
        }

        return new VulkanRecordingReadResult(
            Path.GetFullPath(recordingPath),
            descriptions,
            allShaders,
            eventCounts,
            device,
            cacheReplays,
            cacheReplaySkips,
            missingShaderHashes.Order(StringComparer.OrdinalIgnoreCase).ToArray());
    }

    private static void AddShaderUse(
        JsonElement stageOrPipeline,
        string hashProperty,
        string stageName,
        string entryPoint,
        string? specializationJson,
        IReadOnlyDictionary<string, byte[]> shaderCode,
        ICollection<ShaderArtifact> pipelineShaders,
        ICollection<ShaderArtifact> allShaders,
        ISet<string> seenArtifacts,
        ISet<string> missingShaderHashes,
        ISet<string> pipelineMissingHashes,
        ISet<string> pipelineShaderHashes)
    {
        var hash = NormalizeHash(GetString(stageOrPipeline, hashProperty));
        if (hash is null)
        {
            pipelineMissingHashes.Add("<missing-hash>");
            return;
        }

        pipelineShaderHashes.Add(hash);
        var hasCode = shaderCode.TryGetValue(hash, out var code);
        if (!hasCode)
        {
            missingShaderHashes.Add(hash);
            pipelineMissingHashes.Add(hash);
        }

        var metadata = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["module_hash"] = hash,
            ["stage"] = stageName,
            ["entry_point"] = entryPoint
        };
        if (specializationJson is not null)
            metadata["specialization_json"] = specializationJson;

        var artifact = new ShaderArtifact(
            hash,
            ShaderFormat.SpirV,
            stageName,
            hasCode ? new ReadOnlyMemory<byte>(code!) : ReadOnlyMemory<byte>.Empty,
            metadata);

        pipelineShaders.Add(artifact);
        var key = $"{hash}|{stageName}";
        if (seenArtifacts.Add(key))
            allShaders.Add(artifact);
    }

    private static Dictionary<string, string> BuildFixedFunctionState(
        string kind,
        JsonElement state,
        bool dynamicRendering)
    {
        var result = new Dictionary<string, string>(StringComparer.Ordinal);
        CopyRawProperty(state, result, "flags");
        CopyRawProperty(state, result, "stage_flags");
        CopyRawProperty(state, result, "subpass");
        CopyRawProperty(state, result, "base_pipeline_index");
        CopyRawProperty(state, result, "legacy_render_pass");
        CopyRawProperty(state, result, "replay_compatible");

        if (kind == "compute")
        {
            CopyRawProperty(state, result, "stage");
            CopyRawProperty(state, result, "entry_point");
            CopyRawProperty(state, result, "specialization");
            return result;
        }

        foreach (var name in new[]
        {
            "vertex_input",
            "input_assembly",
            "tessellation",
            "viewport_state",
            "rasterization",
            "multisample",
            "depth_stencil",
            "color_blend",
            "dynamic_state",
            "dynamic_rendering"
        })
        {
            CopyRawProperty(state, result, name);
        }

        result["rendering_mode"] = dynamicRendering ? "dynamic" : "render_pass";
        return result;
    }

    private static Dictionary<string, string> BuildResourceInterface(
        string? layoutHash,
        string? renderPassHash,
        bool dynamicRendering,
        IReadOnlyDictionary<string, JsonElement> pipelineLayouts,
        IReadOnlyDictionary<string, JsonElement> descriptorLayouts,
        IReadOnlyDictionary<string, JsonElement> renderPasses)
    {
        var result = new Dictionary<string, string>(StringComparer.Ordinal);
        if (layoutHash is not null)
        {
            result["pipeline_layout_hash"] = layoutHash;
            if (pipelineLayouts.TryGetValue(layoutHash, out var layout))
            {
                result["pipeline_layout_json"] = layout.GetRawText();
                if (layout.TryGetProperty("set_layouts", out var setLayouts) &&
                    setLayouts.ValueKind == JsonValueKind.Array)
                {
                    var rawDescriptorLayouts = new List<string>();
                    foreach (var item in setLayouts.EnumerateArray())
                    {
                        var hash = NormalizeHash(item.ValueKind == JsonValueKind.String ? item.GetString() : null);
                        if (hash is not null && descriptorLayouts.TryGetValue(hash, out var descriptorLayout))
                            rawDescriptorLayouts.Add(descriptorLayout.GetRawText());
                    }

                    result["descriptor_set_layouts_json"] = string.Join(Environment.NewLine, rawDescriptorLayouts);
                }
            }
        }

        if (renderPassHash is not null)
        {
            result["render_pass_hash"] = renderPassHash;
            if (!dynamicRendering && renderPasses.TryGetValue(renderPassHash, out var renderPass))
                result["render_pass_json"] = renderPass.GetRawText();
        }

        return result;
    }

    private static bool IsPipelineLayoutCompatible(
        string hash,
        IReadOnlyDictionary<string, JsonElement> pipelineLayouts,
        IReadOnlyDictionary<string, JsonElement> descriptorLayouts)
    {
        if (!pipelineLayouts.TryGetValue(hash, out var layout) ||
            !GetBoolean(layout, "replay_compatible", true))
            return false;

        if (!layout.TryGetProperty("set_layouts", out var setLayouts) ||
            setLayouts.ValueKind != JsonValueKind.Array)
            return false;

        foreach (var item in setLayouts.EnumerateArray())
        {
            var descriptorHash = NormalizeHash(item.ValueKind == JsonValueKind.String ? item.GetString() : null);
            if (descriptorHash is null ||
                !descriptorLayouts.TryGetValue(descriptorHash, out var descriptorLayout) ||
                !GetBoolean(descriptorLayout, "replay_compatible", true))
                return false;
        }

        return true;
    }

    private static bool HasUnsupportedPNext(JsonElement pipeline)
    {
        if (GetBoolean(pipeline, "pnext_present", false))
            return true;

        if (GetBoolean(pipeline, "pipeline_pnext_present", false) &&
            !GetBoolean(pipeline, "pipeline_pnext_compatible", false))
            return true;

        var dynamicRendering = GetObjectProperty(pipeline, "dynamic_rendering");
        if (dynamicRendering is not null &&
            GetBoolean(dynamicRendering.Value, "pnext_present", false))
            return true;

        if (pipeline.TryGetProperty("stages", out var stages) &&
            stages.ValueKind == JsonValueKind.Array &&
            stages.EnumerateArray().Any(stage => GetBoolean(stage, "pnext_present", false)))
            return true;

        foreach (var propertyName in new[]
        {
            "vertex_input",
            "input_assembly",
            "tessellation",
            "viewport_state",
            "rasterization",
            "multisample",
            "depth_stencil",
            "color_blend",
            "dynamic_state"
        })
        {
            var state = GetObjectProperty(pipeline, propertyName);
            if (state is not null && GetBoolean(state.Value, "pnext_present", false))
                return true;
        }

        return false;
    }

    private static void CopyRawProperty(JsonElement source, IDictionary<string, string> target, string propertyName)
    {
        if (source.ValueKind == JsonValueKind.Object &&
            source.TryGetProperty(propertyName, out var value) &&
            value.ValueKind is not JsonValueKind.Null and not JsonValueKind.Undefined)
        {
            target[propertyName] = value.GetRawText();
        }
    }

    private static JsonElement? GetObjectProperty(JsonElement source, string name)
    {
        if (source.ValueKind == JsonValueKind.Object &&
            source.TryGetProperty(name, out var value) &&
            value.ValueKind == JsonValueKind.Object)
        {
            return value;
        }

        return null;
    }

    private static string? GetPropertyRaw(JsonElement source, string name)
    {
        if (source.ValueKind == JsonValueKind.Object &&
            source.TryGetProperty(name, out var value) &&
            value.ValueKind is not JsonValueKind.Null and not JsonValueKind.Undefined)
            return value.GetRawText();
        return null;
    }

    private static void CopyRawProperty(JsonElement source, Dictionary<string, string> target, string propertyName) =>
        CopyRawProperty(source, (IDictionary<string, string>)target, propertyName);

    private static string? NormalizeHash(string? hash) =>
        string.IsNullOrWhiteSpace(hash) ? null : hash.Trim().Trim('"').ToLowerInvariant();

    private static string? GetString(JsonElement source, string name) =>
        source.ValueKind == JsonValueKind.Object &&
        source.TryGetProperty(name, out var value) &&
        value.ValueKind == JsonValueKind.String
            ? value.GetString()
            : null;

    private static bool GetBoolean(JsonElement source, string name, bool defaultValue) =>
        source.ValueKind == JsonValueKind.Object &&
        source.TryGetProperty(name, out var value) &&
        value.ValueKind is JsonValueKind.True or JsonValueKind.False
            ? value.GetBoolean()
            : defaultValue;

    private static uint? GetUInt32(JsonElement source, string name) =>
        source.ValueKind == JsonValueKind.Object &&
        source.TryGetProperty(name, out var value) &&
        value.ValueKind == JsonValueKind.Number &&
        value.TryGetUInt32(out var parsed)
            ? parsed
            : null;

    private static long? GetInt64(JsonElement source, string name) =>
        source.ValueKind == JsonValueKind.Object &&
        source.TryGetProperty(name, out var value) &&
        value.ValueKind == JsonValueKind.Number &&
        value.TryGetInt64(out var parsed)
            ? parsed
            : null;

    private static long ReadCount(JsonElement source) =>
        GetInt64(source, "count") ?? 1;

    private static long ReadCount(JsonElement source, string name) =>
        GetInt64(source, name) ?? 0;
}
