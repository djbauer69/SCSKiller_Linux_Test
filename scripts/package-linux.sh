#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="${CONFIG:-Release}"
BUILD_ROOT="${BUILD_ROOT:-$ROOT/build/linux-package}"
STAGE="${STAGE:-$ROOT/dist/scskiller-linux}"

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        printf 'Missing required command: %s\n' "$1" >&2
        exit 2
    fi
}

for command in cmake ninja dotnet glslc; do
    require_command "$command"
done

if ! pkg-config --exists vulkan 2>/dev/null && \
   [[ ! -f /usr/include/vulkan/vulkan.h ]] && \
   [[ ! -f /usr/local/include/vulkan/vulkan.h ]]; then
    printf 'Vulkan development headers were not found. Install your distribution Vulkan SDK/development package first.\n' >&2
    exit 2
fi

rm -rf -- "$STAGE"
mkdir -p -- "$STAGE" "$BUILD_ROOT" \
    "$STAGE/bin" "$STAGE/libexec/cli" \
    "$STAGE/share/doc/scskiller-linux"

printf '[1/4] Building and installing the Vulkan layer...\n'
cmake -S "$ROOT/native/vulkan-layer" \
    -B "$BUILD_ROOT/vulkan-layer" -G Ninja \
    -DCMAKE_BUILD_TYPE="$CONFIG"
cmake --build "$BUILD_ROOT/vulkan-layer" --parallel
cmake --install "$BUILD_ROOT/vulkan-layer" --prefix "$STAGE"

printf '[2/4] Building and installing the Vulkan warmer...\n'
cmake -S "$ROOT/native/vulkan-warmer" \
    -B "$BUILD_ROOT/vulkan-warmer" -G Ninja \
    -DCMAKE_BUILD_TYPE="$CONFIG"
cmake --build "$BUILD_ROOT/vulkan-warmer" --parallel
cmake --install "$BUILD_ROOT/vulkan-warmer" --prefix "$STAGE"

printf '[3/4] Publishing the managed Linux CLI...\n'
dotnet publish "$ROOT/src/SCSKiller.Linux.Cli/SCSKiller.Linux.Cli.csproj" \
    --configuration "$CONFIG" \
    --output "$STAGE/libexec/cli" \
    --self-contained false

printf '[4/4] Adding launcher and documentation...\n'
cat > "$STAGE/bin/scskiller-linux" <<'LAUNCHER'
#!/usr/bin/env bash
set -euo pipefail
BIN_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="$(cd -- "$BIN_DIR/.." && pwd)"
export SCSKILLER_HOME="$PREFIX"
export SCSKILLER_VK_LAYER_DIR="$PREFIX/share/vulkan/explicit_layer.d"
export VK_LAYER_PATH="$SCSKILLER_VK_LAYER_DIR${VK_LAYER_PATH:+:$VK_LAYER_PATH}"
export LD_LIBRARY_PATH="$PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec dotnet "$PREFIX/libexec/cli/scskiller-linux.dll" "$@"
LAUNCHER
chmod 0755 "$STAGE/bin/scskiller-linux"

install -m 0644 "$ROOT/README.md" "$STAGE/share/doc/scskiller-linux/README.md"
install -m 0644 "$ROOT/LINUX.md" "$STAGE/share/doc/scskiller-linux/LINUX.md"
install -m 0644 "$ROOT/native/vulkan-layer/recording-format.md" \
    "$STAGE/share/doc/scskiller-linux/recording-format.md"

if git -C "$ROOT" rev-parse --short HEAD >/dev/null 2>&1; then
    VERSION="$(git -C "$ROOT" rev-parse --short HEAD)"
else
    VERSION="source-tree"
fi
printf 'version=%s\nconfiguration=%s\n' "$VERSION" "$CONFIG" \
    > "$STAGE/VERSION"

printf '\nPackage staged at:\n  %s\n' "$STAGE"
printf 'Launcher:\n  %s/bin/scskiller-linux\n' "$STAGE"
printf 'Required at runtime: .NET 10 runtime and a working Vulkan loader/driver.\n'
