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