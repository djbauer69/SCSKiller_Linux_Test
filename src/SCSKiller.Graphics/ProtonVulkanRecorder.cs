namespace SCSKiller.Graphics;

public sealed record ProtonVulkanRecordOptions(
    string ProtonExecutable,
    string CompatDataPath,
    string WorkDirectory,
    string GameExecutableName,
    string VulkanLayerDirectory,
    string RecordingPath,
    IReadOnlyList<string>? GameArguments = null,
    bool DebugLayer = false);

public sealed record ProtonVulkanRecordResult(
    int ExitCode,
    TimeSpan Elapsed,
    string StandardOutput,
    string StandardError,
    string RecordingPath);

public static class ProtonVulkanRecorder
{
    public static async Task<ProtonVulkanRecordResult> RunAsync(
        ProtonVulkanRecordOptions options,
        CancellationToken cancellationToken = default)
    {
        if (!OperatingSystem.IsLinux())
            throw new PlatformNotSupportedException("The Vulkan recorder is a Linux-only workflow.");

        if (!File.Exists(options.ProtonExecutable))
            throw new FileNotFoundException("Proton executable was not found.", options.ProtonExecutable);

        if (!Directory.Exists(options.VulkanLayerDirectory))
            throw new DirectoryNotFoundException(
                $"Vulkan layer directory was not found: {options.VulkanLayerDirectory}");

        Directory.CreateDirectory(options.WorkDirectory);

        var recordingPath = Path.GetFullPath(options.RecordingPath);
        var recordingDirectory = Path.GetDirectoryName(recordingPath);
        if (!string.IsNullOrWhiteSpace(recordingDirectory))
            Directory.CreateDirectory(recordingDirectory);

        var psi = new System.Diagnostics.ProcessStartInfo
        {
            FileName = options.ProtonExecutable,
            WorkingDirectory = options.WorkDirectory,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
        };

        psi.ArgumentList.Add("run");
        psi.ArgumentList.Add(options.GameExecutableName);

        if (options.GameArguments is not null)
        {
            foreach (var argument in options.GameArguments)
                psi.ArgumentList.Add(argument);
        }

        psi.Environment["STEAM_COMPAT_DATA_PATH"] = options.CompatDataPath;
        psi.Environment["SCSKILLER_VK_RECORD"] = "1";
        psi.Environment["SCSKILLER_VK_RECORD_FILE"] = recordingPath;
        psi.Environment["VK_INSTANCE_LAYERS"] =
            PrependEnvironmentPath("VK_INSTANCE_LAYERS", "VK_LAYER_SCSKILLER");
        psi.Environment["VK_LAYER_PATH"] =
            PrependEnvironmentPath("VK_LAYER_PATH", Path.GetFullPath(options.VulkanLayerDirectory));
        var layerLibraryDirectory = FindLayerLibraryDirectory(options.VulkanLayerDirectory);
        if (layerLibraryDirectory is not null)
        {
            psi.Environment["LD_LIBRARY_PATH"] =
                PrependEnvironmentPath("LD_LIBRARY_PATH", layerLibraryDirectory);
        }

        if (options.DebugLayer)
            psi.Environment["SCSKILLER_VK_DEBUG"] = "1";

        using var process = new System.Diagnostics.Process { StartInfo = psi };
        if (!process.Start())
            throw new InvalidOperationException("Failed to start the Proton Vulkan recording process.");

        var stdoutTask = process.StandardOutput.ReadToEndAsync(cancellationToken);
        var stderrTask = process.StandardError.ReadToEndAsync(cancellationToken);
        var stopwatch = System.Diagnostics.Stopwatch.StartNew();

        await process.WaitForExitAsync(cancellationToken);
        stopwatch.Stop();

        return new ProtonVulkanRecordResult(
            process.ExitCode,
            stopwatch.Elapsed,
            await stdoutTask,
            await stderrTask,
            recordingPath);
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
        return string.IsNullOrWhiteSpace(existing)
            ? value
            : $"{value}{Path.PathSeparator}{existing}";
    }
}
