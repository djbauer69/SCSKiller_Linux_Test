namespace SCSKiller.Graphics;

public sealed record VulkanProcessRecordOptions(
    string Executable,
    string WorkDirectory,
    string VulkanLayerDirectory,
    string RecordingPath,
    IReadOnlyList<string>? Arguments = null,
    bool DebugLayer = false);

public sealed record VulkanProcessRecordResult(
    int ExitCode,
    TimeSpan Elapsed,
    string StandardOutput,
    string StandardError,
    string RecordingPath);

public static class VulkanProcessRecorder
{
    public static async Task<VulkanProcessRecordResult> RunAsync(
        VulkanProcessRecordOptions options,
        CancellationToken cancellationToken = default)
    {
        if (!OperatingSystem.IsLinux())
            throw new PlatformNotSupportedException("The Vulkan recorder is a Linux-only workflow.");

        if (!File.Exists(options.Executable))
            throw new FileNotFoundException("Vulkan executable was not found.", options.Executable);

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
            FileName = options.Executable,
            WorkingDirectory = options.WorkDirectory,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
        };

        if (options.Arguments is not null)
        {
            foreach (var argument in options.Arguments)
                psi.ArgumentList.Add(argument);
        }

        psi.Environment["SCSKILLER_VK_RECORD"] = "1";
        psi.Environment["SCSKILLER_VK_RECORD_FILE"] = recordingPath;
        psi.Environment["VK_INSTANCE_LAYERS"] =
            PrependEnvironmentPath("VK_INSTANCE_LAYERS", "VK_LAYER_SCSKILLER");
        psi.Environment["VK_LAYER_PATH"] =
            PrependEnvironmentPath("VK_LAYER_PATH", Path.GetFullPath(options.VulkanLayerDirectory));

        if (options.DebugLayer)
            psi.Environment["SCSKILLER_VK_DEBUG"] = "1";

        using var process = new System.Diagnostics.Process { StartInfo = psi };
        if (!process.Start())
            throw new InvalidOperationException("Failed to start the Vulkan recording process.");

        var stdoutTask = process.StandardOutput.ReadToEndAsync(cancellationToken);
        var stderrTask = process.StandardError.ReadToEndAsync(cancellationToken);
        var stopwatch = System.Diagnostics.Stopwatch.StartNew();

        await process.WaitForExitAsync(cancellationToken);
        stopwatch.Stop();

        return new VulkanProcessRecordResult(
            process.ExitCode,
            stopwatch.Elapsed,
            await stdoutTask,
            await stderrTask,
            recordingPath);
    }

    private static string PrependEnvironmentPath(string variableName, string value)
    {
        var existing = Environment.GetEnvironmentVariable(variableName);
        return string.IsNullOrWhiteSpace(existing)
            ? value
            : $"{value}{Path.PathSeparator}{existing}";
    }
}
