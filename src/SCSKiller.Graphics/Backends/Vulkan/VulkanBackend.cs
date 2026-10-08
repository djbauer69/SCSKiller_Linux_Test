namespace SCSKiller.Graphics.Backends.Vulkan;

public sealed class VulkanBackend
{
    public GraphicsPath Path => new(GraphicsApi.Vulkan, TranslationBackend.Native, "Vulkan");

    public bool SupportsRuntimeLayer => OperatingSystem.IsLinux();
}
