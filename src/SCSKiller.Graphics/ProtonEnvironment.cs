namespace SCSKiller.Graphics;

public static class ProtonEnvironment
{
    public static IReadOnlyDictionary<string, string> Build(
        string prefix,
        string? recordFile = null,
        string? replayCache = null)
    {
        var result = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["STEAM_COMPAT_DATA_PATH"] = prefix,
            ["SCSKILLER_VK_RECORD"] = "1",
        };

        if (!string.IsNullOrWhiteSpace(recordFile))
            result["SCSKILLER_VK_RECORD_FILE"] = recordFile;

        if (!string.IsNullOrWhiteSpace(replayCache))
            result["SCSKILLER_VK_REPLAY_CACHE"] = replayCache;

        return result;
    }
}