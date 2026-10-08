namespace SCSKiller.Graphics;

public sealed record ProtonWarmOptions(
    string ProtonExecutable,
    string CompatDataPath,
    string WorkDirectory,
    string GameExecutableName,
    string WarmerExecutable,
    int Threads = 0);

public sealed record ProtonWarmResult(
    int ExitCode,
    TimeSpan Elapsed,
    string StandardOutput,
    string StandardError);

public static class ProtonWarmer
{
    public static async Task<ProtonWarmResult> RunAsync(
        ProtonWarmOptions options,
        CancellationToken cancellationToken = default)
    {
        if (!OperatingSystem.IsLinux())
            throw new PlatformNotSupportedException("The Proton warmer is a Linux-only workflow.");

        if (!File.Exists(options.ProtonExecutable))
            throw new FileNotFoundException("Proton executable was not found.", options.ProtonExecutable);

        if (!File.Exists(options.WarmerExecutable))
            throw new FileNotFoundException("SCSKiller Windows warmer was not found.", options.WarmerExecutable);

        Directory.CreateDirectory(options.WorkDirectory);

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
        psi.ArgumentList.Add(options.WarmerExecutable);
        psi.ArgumentList.Add(options.WorkDirectory);
        psi.ArgumentList.Add(options.GameExecutableName);

        if (options.Threads > 0)
        {
            psi.ArgumentList.Add("--threads");
            psi.ArgumentList.Add(options.Threads.ToString(System.Globalization.CultureInfo.InvariantCulture));
        }

        psi.Environment["STEAM_COMPAT_DATA_PATH"] = options.CompatDataPath;
        psi.Environment["SCSKILLER_MODE"] = "warm";

        using var process = new System.Diagnostics.Process { StartInfo = psi };
        if (!process.Start())
            throw new InvalidOperationException("Failed to start the Proton warmer.");

        var stdout = process.StandardOutput.ReadToEndAsync(cancellationToken);
        var stderr = process.StandardError.ReadToEndAsync(cancellationToken);
        var started = System.Diagnostics.Stopwatch.StartNew();

        await process.WaitForExitAsync(cancellationToken);
        started.Stop();

        return new ProtonWarmResult(
            process.ExitCode,
            started.Elapsed,
            await stdout,
            await stderr);
    }
}
