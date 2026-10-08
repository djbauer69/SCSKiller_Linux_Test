namespace SCSKiller.Graphics;

public static class GraphicsPathDetector
{
    public static GraphicsPath ForApi(GraphicsApi api) => api switch
    {
        GraphicsApi.Vulkan => new(GraphicsApi.Vulkan, TranslationBackend.Native, "Vulkan"),
        GraphicsApi.D3D12 => new(GraphicsApi.D3D12, TranslationBackend.Vkd3dProton, "vkd3d-proton"),
        GraphicsApi.D3D11 or GraphicsApi.D3D10 or GraphicsApi.D3D9 or GraphicsApi.D3D8
            => new(api, TranslationBackend.Dxvk, "DXVK"),
        _ => new(GraphicsApi.Unknown, TranslationBackend.Native, null),
    };
}