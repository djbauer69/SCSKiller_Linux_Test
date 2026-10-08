using SCSKiller.Graphics;

if (args.Length == 0 || args[0] is "help" or "--help" or "-h")
{
    Console.WriteLine("SCSKiller Linux experimental CLI");
    Console.WriteLine();
    Console.WriteLine("Commands:");
    Console.WriteLine("  path <vulkan|d3d12|d3d11|d3d10|d3d9|d3d8>  Show the Linux graphics path");
    Console.WriteLine("  record-info                                  Show Vulkan recorder environment");
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

    case "record-info":
        Console.WriteLine("Vulkan recorder:");
        Console.WriteLine("  SCSKILLER_VK_RECORD=1");
        Console.WriteLine("  SCSKILLER_VK_RECORD_FILE=/path/to/record.jsonl");
        Console.WriteLine();
        Console.WriteLine("The recorder is experimental and currently emits diagnostic pipeline events.");
        break;

    default:
        Console.Error.WriteLine("Unknown command. Use help.");
        Environment.ExitCode = 2;
        break;
}