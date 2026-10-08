namespace SCSKiller.Graphics;

public sealed record LinuxRuntimeInfo(
    bool IsLinux,
    bool IsProton,
    string? ProtonPrefix,
    string? ProtonExecutable,
    string? DxvkPath,
    string? Vkd3dPath);

public static class LinuxRuntimeDetector
{
    public static LinuxRuntimeInfo Detect()
    {
        var isLinux = OperatingSystem.IsLinux();
        var prefix = Environment.GetEnvironmentVariable("STEAM_COMPAT_DATA_PATH");
        var proton = FirstNonEmpty(
            Environment.GetEnvironmentVariable("PROTON"),
            Environment.GetEnvironmentVariable("PROTONPATH"));
        var dxvk = Environment.GetEnvironmentVariable("DXVK_PATH");
        var vkd3d = Environment.GetEnvironmentVariable("VKD3D_PROTON_PATH");

        return new LinuxRuntimeInfo(
            isLinux,
            isLinux && !string.IsNullOrWhiteSpace(prefix),
            prefix,
            proton,
            dxvk,
            vkd3d);
    }

    private static string? FirstNonEmpty(params string?[] values) =>
        values.FirstOrDefault(value => !string.IsNullOrWhiteSpace(value));
}