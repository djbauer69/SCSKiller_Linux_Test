namespace SCSKiller.Graphics;

public sealed record VulkanCacheLaunchOptions(
    string RuntimeExecutable,
    string WorkDirectory,
    string VulkanLayerDirectory,
    string ReplayCachePath,
    IReadOnlyList<string>? RuntimeArguments = null,
    string? ProtonCompatDataPath = null);

public sealed record VulkanCacheLaunchResult(
    int ExitCode,
    TimeSpan Elapsed,
    string StandardOutput,
    string StandardError);

/// <summary>
/// Starts a native Vulkan process or a Proton-hosted game with the SCSKiller
/// layer enabled and a previously warmed, driver-owned VkPipelineCache injected
/// when the application creates an empty cache.
/// </summary>
public static class VulkanCacheLauncher
{
    public static async Task<VulkanCacheLaunchResult> RunAsync(
        VulkanCacheLaunchOptions options,
        CancellationToken cancellationToken = default)
    {
        if (!OperatingSystem.IsLinux())
            throw new PlatformNotSupportedException("Vulkan cache launching is a Linux-only workflow.");

        if (!File.Exists(options.RuntimeExecutable))
            throw new FileNotFoundException("Runtime executable was not found.", options.RuntimeExecutable);

        if (!Directory.Exists(options.VulkanLayerDirectory))
            throw new DirectoryNotFoundException(
                $"Vulkan layer directory was not found: {options.VulkanLayerDirectory}");

        if (!File.Exists(options.ReplayCachePath))
            throw new FileNotFoundException("Warmed Vulkan pipeline cache was not found.", options.ReplayCachePath);

        Directory.CreateDirectory(options.WorkDirectory);

        var psi = new System.Diagnostics.ProcessStartInfo
        {
            FileName = options.RuntimeExecutable,
            WorkingDirectory = options.WorkDirectory,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
        };

        if (options.RuntimeArguments is not null)
        {
            foreach (var argument in options.RuntimeArguments)
                psi.ArgumentList.Add(argument);
        }

        if (!string.IsNullOrWhiteSpace(options.ProtonCompatDataPath))
            psi.Environment["STEAM_COMPAT_DATA_PATH"] = options.ProtonCompatDataPath;

        psi.Environment["SCSKILLER_VK_REPLAY_CACHE"] = Path.GetFullPath(options.ReplayCachePath);
        psi.Environment["VK_INSTANCE_LAYERS"] =
            PrependEnvironmentPath("VK_INSTANCE_LAYERS", "VK_LAYER_SCSKILLER");
        psi.Environment["VK_LAYER_PATH"] =
            PrependEnvironmentPath("VK_LAYER_PATH", Path.GetFullPath(options.VulkanLayerDirectory));

        var libraryDirectory = FindLayerLibraryDirectory(options.VulkanLayerDirectory);
        if (libraryDirectory is not null)
        {
            psi.Environment["LD_LIBRARY_PATH"] =
                PrependEnvironmentPath("LD_LIBRARY_PATH", libraryDirectory);
        }

        using var process = new System.Diagnostics.Process { StartInfo = psi };
        if (!process.Start())
            throw new InvalidOperationException("Failed to start the Vulkan cache launch process.");

        var stdoutTask = process.StandardOutput.ReadToEndAsync(cancellationToken);
        var stderrTask = process.StandardError.ReadToEndAsync(cancellationToken);
        var stopwatch = System.Diagnostics.Stopwatch.StartNew();

        await process.WaitForExitAsync(cancellationToken);
        stopwatch.Stop();

        return new VulkanCacheLaunchResult(
            process.ExitCode,
            stopwatch.Elapsed,
            await stdoutTask,
            await stderrTask);
    }

    private static string? FindLayerLibraryDirectory(string layerDirectory)
    {
        var candidate = Path.GetFullPath(
            Path.Combine(layerDirectory, "..", "..", "..", "lib"));
        return Directory.Exists(candidate) ? candidate : null;
    }

    private static string PrependEnvironmentPath(string variableName, string value)
    {
        var existing = Environment.GetEnvironmentVariable(variableName);
        if (string.IsNullOrWhiteSpace(existing))
            return value;

        var entries = existing.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries);
        return entries.Contains(value, StringComparer.Ordinal)
            ? existing
            : $"{value}{Path.PathSeparator}{existing}";
    }
}
