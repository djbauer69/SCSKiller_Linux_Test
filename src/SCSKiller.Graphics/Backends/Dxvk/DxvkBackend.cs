namespace SCSKiller.Graphics.Backends.Dxvk;

public sealed class DxvkBackend
{
    public static IReadOnlySet<GraphicsApi> SupportedApis { get; } =
        new HashSet<GraphicsApi>
        {
            GraphicsApi.D3D8,
            GraphicsApi.D3D9,
            GraphicsApi.D3D10,
            GraphicsApi.D3D11,
        };

    public GraphicsPath Path(GraphicsApi api)
    {
        if (!SupportedApis.Contains(api))
            throw new ArgumentOutOfRangeException(nameof(api), api, "DXVK handles D3D8 through D3D11.");

        return new GraphicsPath(api, TranslationBackend.Dxvk, "DXVK");
    }
}
