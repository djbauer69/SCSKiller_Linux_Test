namespace SCSKiller.Graphics;

public enum TranslationBackend
{
    Native,
    Vkd3dProton,
    Dxvk,
}

public sealed record GraphicsPath(
    GraphicsApi Api,
    TranslationBackend Backend,
    string? RuntimeName = null)
{
    public bool IsVulkanPath => Api == GraphicsApi.Vulkan || Backend is TranslationBackend.Vkd3dProton or TranslationBackend.Dxvk;
}
