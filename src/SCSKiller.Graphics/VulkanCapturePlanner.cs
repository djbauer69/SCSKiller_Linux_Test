namespace SCSKiller.Graphics;

public interface IVulkanCapturePlanner
{
    VulkanPipelinePlan PlanCapture(
        string recordingPath,
        CancellationToken cancellationToken = default);
}

public sealed record VulkanPipelinePlan(
    VulkanRecordingReadResult Recording,
    IReadOnlyList<PipelineDescription> ReplayablePipelines,
    IReadOnlyList<PipelineDescription> UnsupportedPipelines,
    IReadOnlyDictionary<string, int> UnsupportedReasonCounts);

/// <summary>
/// Turns a Vulkan recording into an actionable warming plan. The reader
/// preserves the recorded state; this planner separates pipelines that meet
/// the current reconstruction contract from those that require more capture
/// support, and reports grounded reasons for each exclusion.
/// </summary>
public sealed class VulkanCapturePlanner : IVulkanCapturePlanner
{
    public VulkanPipelinePlan PlanCapture(
        string recordingPath,
        CancellationToken cancellationToken = default)
    {
        var recording = VulkanRecordingReader.Read(recordingPath, cancellationToken);
        var replayable = new List<PipelineDescription>();
        var unsupported = new List<PipelineDescription>();
        var reasonCounts = new SortedDictionary<string, int>(StringComparer.Ordinal);

        foreach (var pipeline in recording.Pipelines)
        {
            cancellationToken.ThrowIfCancellationRequested();

            if (pipeline.BackendMetadata is not { } metadata)
            {
                unsupported.Add(pipeline);
                Increment(reasonCounts, "missing-backend-metadata");
                continue;
            }

            if (!metadata.TryGetValue("replay_compatible", out var compatible) ||
                !string.Equals(compatible, "true", StringComparison.OrdinalIgnoreCase))
            {
                unsupported.Add(pipeline);

                if (metadata.TryGetValue("replay_incompatibility_reasons", out var reasons) &&
                    !string.IsNullOrWhiteSpace(reasons))
                {
                    foreach (var reason in reasons.Split(';', StringSplitOptions.RemoveEmptyEntries)
                                                  .Distinct(StringComparer.Ordinal))
                    {
                        Increment(reasonCounts, reason);
                    }
                }
                else
                {
                    Increment(reasonCounts, "not-marked-replay-compatible");
                }

                continue;
            }

            replayable.Add(pipeline);
        }

        return new VulkanPipelinePlan(
            recording,
            replayable,
            unsupported,
            reasonCounts);
    }

    private static void Increment(IDictionary<string, int> counts, string key) =>
        counts[key] = counts.GetValueOrDefault(key) + 1;
}
