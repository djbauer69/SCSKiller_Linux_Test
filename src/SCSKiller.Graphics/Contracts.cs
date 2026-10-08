namespace SCSKiller.Graphics;

public interface IShaderReader
{
    IReadOnlyList<ShaderArtifact> ReadShaders(string gameDirectory, CancellationToken cancellationToken = default);
}

public interface IGraphicsPathDetector
{
    GraphicsPath Detect(string gameDirectory);
}

public interface IPipelinePlanner
{
    IReadOnlyList<PipelineDescription> Plan(
        IReadOnlyList<ShaderArtifact> shaders,
        CancellationToken cancellationToken = default);
}

public interface IRuntimeRecorder
{
    Task<RecordingSession> StartAsync(
        RecordingOptions options,
        CancellationToken cancellationToken = default);
}

public interface IPipelineWarmer
{
    Task<WarmSummary> WarmAsync(
        IReadOnlyList<PipelineDescription> pipelines,
        WarmOptions options,
        CancellationToken cancellationToken = default);
}

public sealed record RecordingOptions(
    string OutputDirectory,
    GraphicsPath Path,
    bool CaptureShaders = true,
    bool CapturePipelines = true);

public sealed record RecordingSession(
    string OutputPath,
    GraphicsPath Path);

public sealed record WarmOptions(
    int Threads = 0,
    bool FailFast = false);

public sealed record WarmSummary(
    int Requested,
    int Succeeded,
    int Failed,
    TimeSpan Elapsed);
