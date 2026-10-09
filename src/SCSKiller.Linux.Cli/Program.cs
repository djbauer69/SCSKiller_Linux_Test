using SCSKiller.Graphics;
using System.Text.Json;

if (args.Length == 0 || args[0] is "help" or "--help" or "-h")
{
    Console.WriteLine("SCSKiller Linux experimental CLI");
    Console.WriteLine();
    Console.WriteLine("Commands:");
    Console.WriteLine("  path <vulkan|d3d12|d3d11|d3d10|d3d9|d3d8>  Show the Linux graphics path");
    Console.WriteLine("  runtime                                      Show detected Proton/Vulkan environment");
    Console.WriteLine("  inspect-vulkan <capture.jsonl>              Summarize a Vulkan recording without replaying it");
    Console.WriteLine("  record-info                                  Show Vulkan recorder environment");
    Console.WriteLine("  warm-proton <proton> <prefix> <workdir> <game-exe> [warmer.exe] [--threads N]  Run the existing warmer under Proton");
    Console.WriteLine("  record-vulkan <executable> <workdir> <layer-dir> <capture.jsonl> [args...]  Record a native Vulkan process");
    Console.WriteLine("  record-proton <proton> <prefix> <workdir> <game-exe> <layer-dir> <capture.jsonl> [game args...]  Record Vulkan through Proton");
    Console.WriteLine("  warm-vulkan <capture.jsonl> [--input-cache path] [--output-cache path] [--warmer path]  Replay recorded Vulkan pipelines through the driver");
    return;
}

switch (args[0])
{
    case "path" when args.Length > 1:
        if (!Enum.TryParse<GraphicsApi>(args[1], true, out var api))
        {
            Console.Error.WriteLine($"Unknown graphics API: {args[1]}");
            Environment.ExitCode = 2;
            return;
        }
        var path = GraphicsPathDetector.ForApi(api);
        Console.WriteLine($"{path.Api} -> {path.Backend} ({path.RuntimeName ?? "unknown"})");
        break;

    case "runtime":
        var runtime = LinuxRuntimeDetector.Detect();
        Console.WriteLine($"Linux: {runtime.IsLinux}");
        Console.WriteLine($"Proton environment: {runtime.IsProton}");
        Console.WriteLine($"STEAM_COMPAT_DATA_PATH: {runtime.ProtonPrefix ?? "<not set>"}");
        Console.WriteLine($"Proton executable/path: {runtime.ProtonExecutable ?? "<not set>"}");
        Console.WriteLine($"DXVK path: {runtime.DxvkPath ?? "<not set>"}");
        Console.WriteLine($"vkd3d-proton path: {runtime.Vkd3dPath ?? "<not set>"}");
        break;

    case "warm-proton" when args.Length >= 5:
        var proton = args[1];
        var prefix = args[2];
        var workdir = args[3];
        var gameExe = args[4];
        var warmer = args.Length >= 6 ? args[5] : Path.Combine(AppContext.BaseDirectory, "scskiller_warm.exe");
        var threads = 0;

        for (var i = 6; i + 1 < args.Length; i++)
        {
            if (args[i] == "--threads" && int.TryParse(args[++i], out var parsed))
                threads = parsed;
        }

        var result = await ProtonWarmer.RunAsync(
            new ProtonWarmOptions(proton, prefix, workdir, gameExe, warmer, threads));

        Console.Write(result.StandardOutput);
        if (!string.IsNullOrEmpty(result.StandardError))
            Console.Error.Write(result.StandardError);

        Console.WriteLine($"Proton warmer exit code: {result.ExitCode}");
        Environment.ExitCode = result.ExitCode;
        break;

    case "record-vulkan" when args.Length >= 5:
        var recordExecutable = args[1];
        var recordProcessWorkdir = args[2];
        var recordProcessLayerDir = args[3];
        var recordProcessCapture = args[4];
        var recordProcessArguments = args.Length > 5 ? args[5..] : Array.Empty<string>();

        var processRecordResult = await VulkanProcessRecorder.RunAsync(
            new VulkanProcessRecordOptions(
                recordExecutable,
                recordProcessWorkdir,
                recordProcessLayerDir,
                recordProcessCapture,
                recordProcessArguments));

        Console.Write(processRecordResult.StandardOutput);
        if (!string.IsNullOrEmpty(processRecordResult.StandardError))
            Console.Error.Write(processRecordResult.StandardError);

        Console.WriteLine($"Vulkan recording exit code: {processRecordResult.ExitCode}");
        Console.WriteLine($"Recording file: {processRecordResult.RecordingPath}");
        Environment.ExitCode = processRecordResult.ExitCode;
        break;

    case "record-proton" when args.Length >= 7:
        var recordProton = args[1];
        var recordPrefix = args[2];
        var recordWorkdir = args[3];
        var recordGameExe = args[4];
        var recordLayerDir = args[5];
        var recordCapture = args[6];
        var gameArguments = args.Length > 7 ? args[7..] : Array.Empty<string>();

        var recordResult = await ProtonVulkanRecorder.RunAsync(
            new ProtonVulkanRecordOptions(
                recordProton,
                recordPrefix,
                recordWorkdir,
                recordGameExe,
                recordLayerDir,
                recordCapture,
                gameArguments));

        Console.Write(recordResult.StandardOutput);
        if (!string.IsNullOrEmpty(recordResult.StandardError))
            Console.Error.Write(recordResult.StandardError);

        Console.WriteLine($"Vulkan recording exit code: {recordResult.ExitCode}");
        Console.WriteLine($"Recording file: {recordResult.RecordingPath}");
        Environment.ExitCode = recordResult.ExitCode;
        break;

    case "warm-vulkan" when args.Length >= 2:
        var capture = args[1];
        string? inputCache = null;
        string? outputCache = null;
        var nativeWarmer = Path.Combine(AppContext.BaseDirectory, "scskiller-vulkan-warmer");

        for (var i = 2; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--input-cache" when i + 1 < args.Length:
                    inputCache = args[++i];
                    break;
                case "--output-cache" when i + 1 < args.Length:
                    outputCache = args[++i];
                    break;
                case "--warmer" when i + 1 < args.Length:
                    nativeWarmer = args[++i];
                    break;
                default:
                    Console.Error.WriteLine($"Unknown warm-vulkan option: {args[i]}");
                    Environment.ExitCode = 2;
                    return;
            }
        }

        var warmResult = await VulkanWarmer.RunAsync(
            new VulkanWarmOptions(nativeWarmer, capture, inputCache, outputCache));

        Console.Write(warmResult.StandardOutput);
        if (!string.IsNullOrEmpty(warmResult.StandardError))
            Console.Error.Write(warmResult.StandardError);

        Console.WriteLine($"Vulkan warmer exit code: {warmResult.ExitCode}");
        Environment.ExitCode = warmResult.ExitCode;
        break;

    case "inspect-vulkan" when args.Length == 2:
        Environment.ExitCode = InspectVulkanRecording(args[1]);
        break;

    case "record-info":
        Console.WriteLine("SCSKILLER_VK_RECORD=1");
        Console.WriteLine("SCSKILLER_VK_RECORD_FILE=/path/to/record.jsonl");
        Console.WriteLine("Recorder status: experimental diagnostic capture");
        break;

    default:
        Console.Error.WriteLine("Unknown command. Use help.");
        Environment.ExitCode = 2;
        break;
}
static int InspectVulkanRecording(string path)
{
    if (!File.Exists(path))
    {
        Console.Error.WriteLine($"Vulkan recording was not found: {path}");
        return 2;
    }

    var eventCounts = new SortedDictionary<string, int>(StringComparer.Ordinal);
    var shaderHashes = new HashSet<string>(StringComparer.Ordinal);
    string? gpuName = null;
    string? cacheUuid = null;
    uint vendorId = 0;
    uint deviceId = 0;
    uint driverVersion = 0;
    long computePipelines = 0;
    long graphicsPipelines = 0;
    long dynamicRenderingPipelines = 0;
    long incompatibleGraphicsPipelines = 0;
    long replayedCaches = 0;
    long skippedCaches = 0;
    var lineNumber = 0;

    try
    {
        foreach (var line in File.ReadLines(path))
        {
            lineNumber++;
            if (string.IsNullOrWhiteSpace(line))
                continue;

            using var document = JsonDocument.Parse(line);
            var root = document.RootElement;
            if (!root.TryGetProperty("event", out var eventProperty) ||
                eventProperty.ValueKind != JsonValueKind.String)
            {
                Console.Error.WriteLine($"Malformed recording at line {lineNumber}: missing event name.");
                return 2;
            }

            var eventName = eventProperty.GetString()!;
            eventCounts[eventName] = eventCounts.GetValueOrDefault(eventName) + 1;

            if (eventName == "shader_module_code" &&
                root.TryGetProperty("hash", out var hashProperty) &&
                hashProperty.ValueKind == JsonValueKind.String)
            {
                shaderHashes.Add(hashProperty.GetString()!);
            }

            if (eventName == "physical_device_identity")
            {
                if (root.TryGetProperty("device_name", out var nameProperty) &&
                    nameProperty.ValueKind == JsonValueKind.String)
                    gpuName = nameProperty.GetString();
                if (root.TryGetProperty("pipeline_cache_uuid", out var uuidProperty) &&
                    uuidProperty.ValueKind == JsonValueKind.String)
                    cacheUuid = uuidProperty.GetString();
                if (root.TryGetProperty("vendor_id", out var property) && property.TryGetUInt32(out var parsedVendor))
                    vendorId = parsedVendor;
                if (root.TryGetProperty("device_id", out property) && property.TryGetUInt32(out var parsedDevice))
                    deviceId = parsedDevice;
                if (root.TryGetProperty("driver_version", out property) && property.TryGetUInt32(out var parsedDriver))
                    driverVersion = parsedDriver;
            }

            if (eventName == "compute_pipeline_state" &&
                root.TryGetProperty("pipelines", out var computeArray) &&
                computeArray.ValueKind == JsonValueKind.Array)
            {
                computePipelines += computeArray.GetArrayLength();
            }

            if (eventName == "graphics_pipeline_state" &&
                root.TryGetProperty("pipelines", out var graphicsArray) &&
                graphicsArray.ValueKind == JsonValueKind.Array)
            {
                foreach (var pipeline in graphicsArray.EnumerateArray())
                {
                    graphicsPipelines++;
                    if (pipeline.TryGetProperty("dynamic_rendering", out var dynamicRendering) &&
                        dynamicRendering.ValueKind == JsonValueKind.Object)
                        dynamicRenderingPipelines++;

                    if (pipeline.TryGetProperty("replay_compatible", out var compatible) &&
                        compatible.ValueKind == JsonValueKind.False)
                        incompatibleGraphicsPipelines++;
                }
            }

            if (eventName == "pipeline_cache_replay")
                replayedCaches += ReadCount(root);
            if (eventName == "pipeline_cache_replay_skipped")
                skippedCaches += ReadCount(root);
        }
    }
    catch (JsonException exception)
    {
        Console.Error.WriteLine($"Invalid JSON at recording line {lineNumber}: {exception.Message}");
        return 2;
    }
    catch (IOException exception)
    {
        Console.Error.WriteLine($"Could not read Vulkan recording: {exception.Message}");
        return 2;
    }

    Console.WriteLine($"Recording: {Path.GetFullPath(path)}");
    Console.WriteLine(gpuName is null
        ? "GPU: identity was not captured"
        : $"GPU: {gpuName} (vendor=0x{vendorId:x4}, device=0x{deviceId:x4}, driver={driverVersion}, cache UUID={cacheUuid ?? "unknown"})");
    Console.WriteLine($"Unique captured SPIR-V modules: {shaderHashes.Count}");
    Console.WriteLine($"Compute pipelines: {computePipelines}");
    Console.WriteLine($"Graphics pipelines: {graphicsPipelines} (dynamic rendering: {dynamicRenderingPipelines}, marked incompatible: {incompatibleGraphicsPipelines})");
    Console.WriteLine($"Driver cache replays: {replayedCaches}; skipped: {skippedCaches}");
    Console.WriteLine("Events:");
    foreach (var item in eventCounts)
        Console.WriteLine($"  {item.Key}: {item.Value}");

    return 0;

    static long ReadCount(JsonElement root) =>
        root.TryGetProperty("count", out var count) && count.TryGetInt64(out var value)
            ? value
            : 1;
}
