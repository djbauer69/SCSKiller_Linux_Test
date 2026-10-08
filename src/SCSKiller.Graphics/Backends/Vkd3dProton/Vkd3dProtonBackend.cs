namespace SCSKiller.Graphics.Backends.Vkd3dProton;

public sealed class Vkd3dProtonBackend
{
    public GraphicsPath Path => new(GraphicsApi.D3D12, TranslationBackend.Vkd3dProton, "vkd3d-proton");
}
