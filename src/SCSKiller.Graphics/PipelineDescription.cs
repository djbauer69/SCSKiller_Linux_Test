namespace SCSKiller.Graphics;

public enum ShaderFormat
{
    Unknown,
    Dxbc,
    Dxil,
    SpirV,
}

public sealed record ShaderArtifact(
    string Hash,
    ShaderFormat Format,
    string Stage,
    ReadOnlyMemory<byte> Code,
    IReadOnlyDictionary<string, string>? Metadata = null);

public sealed record PipelineDescription(
    IReadOnlyList<ShaderArtifact> Shaders,
    IReadOnlyDictionary<string, string>? FixedFunctionState = null,
    IReadOnlyDictionary<string, string>? ResourceInterface = null,
    IReadOnlyDictionary<string, string>? BackendMetadata = null);
