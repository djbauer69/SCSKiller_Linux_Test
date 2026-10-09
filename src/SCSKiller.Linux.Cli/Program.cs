using SCSKiller.Graphics;

if (args.Length == 0 || args[0] is "help" or "--help" or "-h")
{
    Console.WriteLine("SCSKiller Linux experimental CLI");
    Console.WriteLine();
    Console.WriteLine("Commands:");
    Console.WriteLine("  path <vulkan|d3d12|d3d11|d3d10|d3d9|d3d8>  Show the Linux graphics path");
    Console.WriteLine("  runtime                                      Show detected Proton/Vulkan environment");
    Console.WriteLine("  inspect-vulkan <capture.jsonl>              Summarize a Vulkan recording without replaying it");
    Console.WriteLine("  plan-vulkan <capture.jsonl>                 Classify pipelines by replay compatibility and show skip reasons");
    Console.WriteLine("  record-info                                  Show Vulkan recorder environment");
    Console.WriteLine("  warm-proton <proton> <prefix> <workdir> <game-exe> [warmer.exe] [--threads N]  Run the existing warmer under Proton");
    Console.WriteLine("  record-vulkan <executable> <workdir> <layer-dir> <capture.jsonl> [args...]  Record a native Vulkan process");
    Console.WriteLine("  record-proton <proton> <prefix> <workdir> <game-exe> <layer-dir> <capture.jsonl> [game args...]  Record Vulkan through Proton");
    Console.WriteLine("  run-vulkan <executable> <workdir> <layer-dir> <cache.bin> [app args...]  Launch a native Vulkan app with a warmed cache");
    Console.WriteLine("  run-proton-vulkan <proton> <prefix> <workdir> <game-exe> <layer-dir> <cache.bin> [game args...]  Launch Proton with a warmed cache");
    Console.WriteLine("  warm-vulkan <capture.jsonl> [--input-cache path] [--output-cache path] [--warmer path] [--require-complete]  Replay captured Vulkan pipelines");
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

    case "run-vulkan" when args.Length >= 5:
        var runVulkanResult = await VulkanCacheLauncher.RunAsync(
            new VulkanCacheLaunchOptions(
                args[1],
                args[2],
                args[3],
                args[4],
                args.Length > 5 ? args[5..] : Array.Empty<string>()));

        Console.Write(runVulkanResult.StandardOutput);
        if (!string.IsNullOrEmpty(runVulkanResult.StandardError))
            Console.Error.Write(runVulkanResult.StandardError);

        Console.WriteLine($"Vulkan cache launch exit code: {runVulkanResult.ExitCode}");
        Environment.ExitCode = runVulkanResult.ExitCode;
        break;

    case "run-proton-vulkan" when args.Length >= 7:
        var runProtonArguments = new[] { "run", args[4] }
            .Concat(args.Length > 7 ? args[7..] : Array.Empty<string>())
            .ToArray();

        var runProtonResult = await VulkanCacheLauncher.RunAsync(
            new VulkanCacheLaunchOptions(
                args[1],
                args[3],
                args[5],
                args[6],
                runProtonArguments,
                args[2]));

        Console.Write(runProtonResult.StandardOutput);
        if (!string.IsNullOrEmpty(runProtonResult.StandardError))
            Console.Error.Write(runProtonResult.StandardError);

        Console.WriteLine($"Proton Vulkan cache launch exit code: {runProtonResult.ExitCode}");
        Environment.ExitCode = runProtonResult.ExitCode;
        break;

    case "warm-vulkan" when args.Length >= 2:
        var capture = args[1];
        string? inputCache = null;
        string? outputCache = null;
        var nativeWarmer = FindNativeWarmerExecutable();
        var requireCompleteReplay = false;

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
                case "--require-complete":
                    requireCompleteReplay = true;
                    break;
                default:
                    Console.Error.WriteLine($"Unknown warm-vulkan option: {args[i]}");
                    Environment.ExitCode = 2;
                    return;
            }
        }

        var warmResult = await VulkanWarmer.RunAsync(
            new VulkanWarmOptions(nativeWarmer, capture, inputCache, outputCache, requireCompleteReplay));

        Console.Write(warmResult.StandardOutput);
        if (!string.IsNullOrEmpty(warmResult.StandardError))
            Console.Error.Write(warmResult.StandardError);

        Console.WriteLine($"Vulkan warmer exit code: {warmResult.ExitCode}");
        Environment.ExitCode = warmResult.ExitCode;
        break;

    case "inspect-vulkan" when args.Length == 2:
        Environment.ExitCode = InspectVulkanRecording(args[1]);
        break;

    case "plan-vulkan" when args.Length == 2:
        Environment.ExitCode = PlanVulkanRecording(args[1]);
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
static int PlanVulkanRecording(string path)
{
    VulkanPipelinePlan plan;
    try
    {
        plan = new VulkanCapturePlanner().PlanCapture(path);
    }
    catch (Exception exception) when (
        exception is IOException or InvalidDataException or ArgumentException)
    {
        Console.Error.WriteLine($"Could not plan Vulkan recording: {exception.Message}");
        return 2;
    }

    var all = plan.Recording.Pipelines;
    var replayableCompute = CountKind(plan.ReplayablePipelines, "compute");
    var replayableGraphics = CountKind(plan.ReplayablePipelines, "graphics");
    var unsupportedCompute = CountKind(plan.UnsupportedPipelines, "compute");
    var unsupportedGraphics = CountKind(plan.UnsupportedPipelines, "graphics");

    Console.WriteLine($"Recording: {plan.Recording.RecordingPath}");
    Console.WriteLine($"Pipeline plan: {all.Count} total; {plan.ReplayablePipelines.Count} replayable; {plan.UnsupportedPipelines.Count} unsupported");
    Console.WriteLine($"Replayable: compute {replayableCompute}, graphics {replayableGraphics}");
    Console.WriteLine($"Unsupported: compute {unsupportedCompute}, graphics {unsupportedGraphics}");

    if (plan.UnsupportedReasonCounts.Count == 0)
    {
        Console.WriteLine("Unsupported reasons: none");
    }
    else
    {
        Console.WriteLine("Unsupported reasons:");
        foreach (var reason in plan.UnsupportedReasonCounts)
            Console.WriteLine($"  {reason.Key}: {reason.Value}");
    }

    return 0;

    static int CountKind(
        IReadOnlyList<PipelineDescription> pipelines,
        string kind) =>
        pipelines.Count(pipeline =>
            pipeline.BackendMetadata is { } metadata &&
            metadata.TryGetValue("pipeline_kind", out var value) &&
            string.Equals(value, kind, StringComparison.Ordinal));
}

static int InspectVulkanRecording(string path)
{
    VulkanRecordingReadResult recording;
    try
    {
        recording = VulkanRecordingReader.Read(path);
    }
    catch (Exception exception) when (
        exception is IOException or InvalidDataException or ArgumentException)
    {
        Console.Error.WriteLine($"Could not inspect Vulkan recording: {exception.Message}");
        return 2;
    }

    var shaders = recording.Shaders
        .Select(shader => shader.Hash)
        .Distinct(StringComparer.OrdinalIgnoreCase)
        .Count();

    var computePipelines = recording.Pipelines.Count(pipeline =>
        pipeline.BackendMetadata is { } metadata &&
        metadata.TryGetValue("pipeline_kind", out var kind) &&
        kind == "compute");

    var graphics = recording.Pipelines.Where(pipeline =>
        pipeline.BackendMetadata is { } metadata &&
        metadata.TryGetValue("pipeline_kind", out var kind) &&
        kind == "graphics").ToArray();

    var dynamicRendering = graphics.Count(pipeline =>
        pipeline.BackendMetadata is { } metadata &&
        metadata.TryGetValue("dynamic_rendering", out var value) &&
        value == "true");

    var incompatibleGraphics = graphics.Count(pipeline =>
        pipeline.BackendMetadata is { } metadata &&
        metadata.TryGetValue("replay_compatible", out var value) &&
        value == "false");

    Console.WriteLine($"Recording: {recording.RecordingPath}");
    Console.WriteLine(recording.Device is null
        ? "GPU: identity was not captured"
        : $"GPU: {recording.Device.Name} (vendor=0x{recording.Device.VendorId:x4}, device=0x{recording.Device.DeviceId:x4}, driver={recording.Device.DriverVersion}, cache UUID={recording.Device.PipelineCacheUuid})");
    Console.WriteLine($"Unique captured SPIR-V modules: {shaders}");
    Console.WriteLine($"Compute pipelines: {computePipelines}");
    Console.WriteLine($"Graphics pipelines: {graphics.Length} (dynamic rendering: {dynamicRendering}, marked incompatible: {incompatibleGraphics})");
    Console.WriteLine($"Driver cache injections/merges: {recording.CacheReplays}; skipped: {recording.CacheReplaySkips}");
    Console.WriteLine($"Pipelines with missing SPIR-V bytes: {recording.Pipelines.Count(pipeline => pipeline.BackendMetadata is { } metadata && metadata.TryGetValue("missing_shader_hashes", out var missing) && !string.IsNullOrEmpty(missing))}");
    Console.WriteLine("Events:");
    foreach (var item in recording.EventCounts)
        Console.WriteLine($"  {item.Key}: {item.Value}");

    return 0;
}

static string FindNativeWarmerExecutable()
{
    var packageRoot = Environment.GetEnvironmentVariable("SCSKILLER_HOME");
    if (!string.IsNullOrWhiteSpace(packageRoot))
    {
        var packagedWarmer = Path.Combine(packageRoot, "bin", "scskiller-vulkan-warmer");
        if (File.Exists(packagedWarmer))
            return packagedWarmer;
    }

    return Path.Combine(AppContext.BaseDirectory, "scskiller-vulkan-warmer");
}
