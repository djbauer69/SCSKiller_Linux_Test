namespace SCSKiller.Graphics;

public sealed record VulkanWarmOptions(
    string WarmerExecutable,
    string RecordingPath,
    string? InputCachePath = null,
    string? OutputCachePath = null,
    bool RequireComplete = false);

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

        var standardOutput = await stdoutTask;
        var standardError = await stderrTask;
        var exitCode = process.ExitCode;

        if (exitCode == 0 && options.RequireComplete &&
            HasIncompleteReplaySummary(standardOutput))
        {
            exitCode = 3;
            standardError +=
                (string.IsNullOrEmpty(standardError) ? string.Empty : Environment.NewLine) +
                "Incomplete Vulkan replay: one or more captured pipelines were skipped or failed. " +
                "Review the replay totals and inspect the recording before treating it as warmed.";
        }

        return new VulkanWarmResult(
            exitCode,
            stopwatch.Elapsed,
            standardOutput,
            standardError);
    }

    private static bool HasIncompleteReplaySummary(string output)
    {
        var foundSummary = false;

        foreach (var rawLine in output.Split((char)10, StringSplitOptions.RemoveEmptyEntries))
        {
            var line = rawLine.Trim();
            if (line.StartsWith("Compute replay: requested ", StringComparison.Ordinal) ||
                line.StartsWith("Graphics replay: requested ", StringComparison.Ordinal))
            {
                foundSummary = true;
                var colon = line.IndexOf(':');
                if (colon < 0)
                    return true;

                var parts = line[(colon + 1)..].Split(',');
                long requested = -1;
                long compiled = -1;
                foreach (var part in parts)
                {
                    var tokens = part.Trim().Split(' ', StringSplitOptions.RemoveEmptyEntries);
                    if (tokens.Length < 2 || !long.TryParse(tokens[^1], out var value))
                        continue;

                    switch (tokens[0])
                    {
                        case "requested": requested = value; break;
                        case "compiled": compiled = value; break;
                        case "skipped" when value > 0: return true;
                        case "failed" when value > 0: return true;
                    }
                }

                if (requested < 0 || compiled < 0 || requested != compiled)
                    return true;
            }

            const string rayTracingMarker = "ray-tracing pipelines skipped:";
            var rayTracingIndex = line.IndexOf(rayTracingMarker, StringComparison.Ordinal);
            if (rayTracingIndex >= 0)
            {
                var valueText = line[(rayTracingIndex + rayTracingMarker.Length)..]
                    .TrimStart()
                    .Split(',', StringSplitOptions.RemoveEmptyEntries)[0]
                    .Trim();
                if (!long.TryParse(valueText, out var skippedRayTracing) || skippedRayTracing > 0)
                    return true;
            }
        }

        return !foundSummary;
    }
}
