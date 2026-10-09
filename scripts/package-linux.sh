#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="${CONFIG:-Release}"
BUILD_KDE_UI="${BUILD_KDE_UI:-0}"
BUILD_ROOT="${BUILD_ROOT:-$ROOT/build/linux-package}"
STAGE="${STAGE:-$ROOT/dist/scskiller-linux}"

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        printf 'Missing required command: %s\n' "$1" >&2
        exit 2
    fi
}

for command in cmake ninja dotnet realpath; do
    require_command "$command"
done

if ! pkg-config --exists vulkan 2>/dev/null && \
   [[ ! -f /usr/include/vulkan/vulkan.h ]] && \
   [[ ! -f /usr/local/include/vulkan/vulkan.h ]]; then
    printf 'Vulkan development headers were not found. Install your distribution Vulkan SDK/development package first.\n' >&2
    exit 2
fi

ROOT_REAL="$(realpath "$ROOT")"
STAGE_REAL="$(realpath -m -- "$STAGE")"
BUILD_ROOT_REAL="$(realpath -m -- "$BUILD_ROOT")"
HOME_REAL="$(realpath -m -- "$HOME")"

UNSAFE_STAGE=0
if [[ "$STAGE_REAL" == "/" || "$STAGE_REAL" == "$ROOT_REAL" || "$STAGE_REAL" == "$HOME_REAL" ]]; then
    UNSAFE_STAGE=1
fi

# Never allow rm -rf of the source tree, any ancestor containing it, or a path
# that overlaps the build directory in either direction. Stage and build roots
# may be siblings underneath the checkout (as in CI), but must not overlap.
case "$ROOT_REAL/" in
    "$STAGE_REAL/"*) UNSAFE_STAGE=1 ;;
esac
case "$BUILD_ROOT_REAL/" in
    "$STAGE_REAL/"*) UNSAFE_STAGE=1 ;;
esac
case "$STAGE_REAL/" in
    "$BUILD_ROOT_REAL/"*) UNSAFE_STAGE=1 ;;
esac

if [[ "$UNSAFE_STAGE" == "1" ]]; then
    printf 'Refusing unsafe package staging directory: %s (build root: %s)\n' \
        "$STAGE_REAL" "$BUILD_ROOT_REAL" >&2
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

case "$(uname -m)" in
    x86_64|amd64) RID="linux-x64" ;;
    aarch64|arm64) RID="linux-arm64" ;;
    *)
        printf 'Unsupported package architecture: %s\n' "$(uname -m)" >&2
        exit 2
        ;;
esac

printf '[3/4] Publishing the self-contained Linux CLI for %s...\n' "$RID"
dotnet publish "$ROOT/src/SCSKiller.Linux.Cli/SCSKiller.Linux.Cli.csproj" \
    --configuration "$CONFIG" \
    --runtime "$RID" \
    --output "$STAGE/libexec/cli" \
    --self-contained true

printf '[4/4] Adding launcher and documentation...\n'
cat > "$STAGE/bin/scskiller-linux" <<'LAUNCHER'
#!/usr/bin/env bash
set -euo pipefail
SCRIPT_PATH="$(realpath -- "${BASH_SOURCE[0]}")"
BIN_DIR="$(cd -- "$(dirname -- "$SCRIPT_PATH")" && pwd)"
PREFIX="$(cd -- "$BIN_DIR/.." && pwd)"
export SCSKILLER_HOME="$PREFIX"
export SCSKILLER_VK_LAYER_DIR="$PREFIX/share/vulkan/explicit_layer.d"
export VK_LAYER_PATH="$SCSKILLER_VK_LAYER_DIR${VK_LAYER_PATH:+:$VK_LAYER_PATH}"
export LD_LIBRARY_PATH="$PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$PREFIX/libexec/cli/scskiller-linux" "$@"
LAUNCHER
chmod 0755 "$STAGE/bin/scskiller-linux"

install -m 0644 "$ROOT/README.md" "$STAGE/share/doc/scskiller-linux/README.md"
install -m 0644 "$ROOT/LINUX.md" "$STAGE/share/doc/scskiller-linux/LINUX.md"
install -m 0644 "$ROOT/native/vulkan-layer/recording-format.md" \
    "$STAGE/share/doc/scskiller-linux/recording-format.md"
install -m 0644 "$ROOT/LICENSE" "$STAGE/share/doc/scskiller-linux/LICENSE"
install -m 0644 "$ROOT/LICENSE-EXCEPTION.txt" "$STAGE/share/doc/scskiller-linux/LICENSE-EXCEPTION.txt"
install -m 0644 "$ROOT/THIRD-PARTY-NOTICES.md" "$STAGE/share/doc/scskiller-linux/THIRD-PARTY-NOTICES.md"
install -m 0755 "$ROOT/scripts/install-user-package.sh" "$STAGE/install-user-package.sh"

if [[ "$BUILD_KDE_UI" == "1" ]]; then
    printf '[optional] Building and installing the Qt/Kirigami desktop app...\n'
    cmake -S "$ROOT/gui/kde" \
        -B "$BUILD_ROOT/kde-ui" -G Ninja \
        -DCMAKE_BUILD_TYPE="$CONFIG"
    cmake --build "$BUILD_ROOT/kde-ui" --parallel
    cmake --install "$BUILD_ROOT/kde-ui" --prefix "$STAGE"
elif [[ "$BUILD_KDE_UI" != "0" ]]; then
    printf 'BUILD_KDE_UI must be 0 or 1, got: %s\n' "$BUILD_KDE_UI" >&2
    exit 2
fi

if git -C "$ROOT" rev-parse --short HEAD >/dev/null 2>&1; then
    VERSION="$(git -C "$ROOT" rev-parse --short HEAD)"
else
    VERSION="source-tree"
fi
printf 'version=%s\nconfiguration=%s\n' "$VERSION" "$CONFIG" \
    > "$STAGE/VERSION"

printf '\nPackage staged at:\n  %s\n' "$STAGE"
printf 'Launcher:\n  %s/bin/scskiller-linux\n' "$STAGE"
printf 'Required at runtime: a working Vulkan loader and GPU driver/ICD. The managed CLI runtime is bundled.\n'
if [[ "$BUILD_KDE_UI" == "1" ]]; then
    printf 'KDE UI included; Qt 6 and KDE Kirigami runtime libraries must be installed.\n'
fi
