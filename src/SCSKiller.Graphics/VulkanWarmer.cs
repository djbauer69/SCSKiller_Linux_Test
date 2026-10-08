namespace SCSKiller.Graphics;

public sealed record VulkanWarmOptions(
    string WarmerExecutable,
    string RecordingPath,
    string? InputCachePath = null,
    string? OutputCachePath = null);

public sealed record VulkanWarmResult(
    int ExitCode,
    TimeSpan Elapsed,
    string StandardOutput,
    string StandardError);

public static class VulkanWarmer
{
    public static async Task<VulkanWarmResult> RunAsync(
        VulkanWarmOptions options,
        CancellationToken cancellationToken = default)
    {
        if (!OperatingSystem.IsLinux())
            throw new PlatformNotSupportedException("The Vulkan warmer is a Linux-only workflow.");

        if (!File.Exists(options.WarmerExecutable))
            throw new FileNotFoundException("SCSKiller Vulkan warmer was not found.", options.WarmerExecutable);

        if (!File.Exists(options.RecordingPath))
            throw new FileNotFoundException("Vulkan recording was not found.", options.RecordingPath);

        var psi = new System.Diagnostics.ProcessStartInfo
        {
            FileName = options.WarmerExecutable,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
        };

        psi.ArgumentList.Add(options.RecordingPath);

        if (!string.IsNullOrWhiteSpace(options.OutputCachePath))
        {
            psi.ArgumentList.Add(options.InputCachePath ?? "-");
            psi.ArgumentList.Add(options.OutputCachePath);
        }
        else if (!string.IsNullOrWhiteSpace(options.InputCachePath))
        {
            psi.ArgumentList.Add(options.InputCachePath);
        }

        using var process = new System.Diagnostics.Process { StartInfo = psi };
        if (!process.Start())
            throw new InvalidOperationException("Failed to start the Vulkan warmer.");

        var stdoutTask = process.StandardOutput.ReadToEndAsync(cancellationToken);
        var stderrTask = process.StandardError.ReadToEndAsync(cancellationToken);
        var stopwatch = System.Diagnostics.Stopwatch.StartNew();

        await process.WaitForExitAsync(cancellationToken);
        stopwatch.Stop();

        return new VulkanWarmResult(
            process.ExitCode,
            stopwatch.Elapsed,
            await stdoutTask,
            await stderrTask);
    }
}
