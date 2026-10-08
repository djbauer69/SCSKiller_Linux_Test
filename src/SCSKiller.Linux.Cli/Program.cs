using SCSKiller.Graphics;

if (args.Length == 0 || args[0] is "help" or "--help" or "-h")
{
    Console.WriteLine("SCSKiller Linux experimental CLI");
    Console.WriteLine();
    Console.WriteLine("Commands:");
    Console.WriteLine("  path <vulkan|d3d12|d3d11|d3d10|d3d9|d3d8>  Show the Linux graphics path");
    Console.WriteLine("  runtime                                      Show detected Proton/Vulkan environment");
    Console.WriteLine("  record-info                                  Show Vulkan recorder environment");
    Console.WriteLine("  warm-proton <proton> <prefix> <workdir> <game-exe> [warmer.exe] [--threads N]  Run the existing warmer under Proton");
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