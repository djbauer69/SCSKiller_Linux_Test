#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    printf 'Usage: %s <extracted-scskiller-linux-package-directory>\n' "$0" >&2
    exit 2
fi

SOURCE="$(realpath -- "$1")"
DEST="${DEST:-$HOME/.local/opt/scskiller-linux}"
BIN_DIR="${BIN_DIR:-$HOME/.local/bin}"
DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
APPLICATIONS_DIR="${APPLICATIONS_DIR:-$DATA_HOME/applications}"

if [[ ! -d "$SOURCE" || ! -x "$SOURCE/bin/scskiller-linux" ]]; then
    printf 'Source does not look like a SCSKiller Linux package: %s\n' "$SOURCE" >&2
    exit 2
fi

DEST_INPUT="$DEST"
if [[ -L "$DEST_INPUT" ]]; then
    printf 'Refusing to install through a symlinked destination: %s\n' "$DEST_INPUT" >&2
    exit 2
fi
DEST="$(realpath -m -- "$DEST_INPUT")"
HOME_REAL="$(realpath -- "$HOME")"
if [[ "$DEST" == "/" || "$DEST" == "$HOME_REAL" || "$DEST" == "$SOURCE" || "$DEST" == "$SOURCE"/* || "$SOURCE" == "$DEST"/* ]]; then
    printf 'Refusing unsafe or recursive installation directory: %s\n' "$DEST" >&2
    exit 2
fi

if [[ -e "$DEST" ]]; then
    if [[ ! -d "$DEST" ]]; then
        printf 'Refusing to replace a non-directory installation target: %s\n' "$DEST" >&2
        exit 2
    fi
    if [[ -n "$(find "$DEST" -mindepth 1 -maxdepth 1 -print -quit)" ]] &&
       [[ ! -e "$DEST/VERSION" || ! -x "$DEST/bin/scskiller-linux" ||
          ! -s "$DEST/share/vulkan/explicit_layer.d/layer.json" ||
          ! -s "$DEST/share/doc/scskiller-linux/LICENSE" ]]; then
        printf 'Refusing to copy over an unrelated non-empty directory: %s\n' "$DEST" >&2
        exit 2
    fi
fi

CLI_LINK="$BIN_DIR/scskiller-linux"
if [[ -e "$CLI_LINK" && ! -L "$CLI_LINK" ]]; then
    printf 'Refusing to replace an existing non-symlink: %s\n' "$CLI_LINK" >&2
    exit 2
fi
if [[ -L "$CLI_LINK" ]]; then
    EXISTING_CLI_TARGET="$(realpath -m -- "$CLI_LINK")"
    EXPECTED_CLI_TARGET="$(realpath -m -- "$DEST/bin/scskiller-linux")"
    if [[ "$EXISTING_CLI_TARGET" != "$EXPECTED_CLI_TARGET" ]]; then
        printf 'Refusing to replace a symlink owned by another application: %s -> %s\n' \
            "$CLI_LINK" "$EXISTING_CLI_TARGET" >&2
        exit 2
    fi
fi

if [[ -x "$SOURCE/bin/scskiller-kde" && -f "$SOURCE/share/applications/scskiller-kde.desktop" ]]; then
    DESKTOP_TARGET="$APPLICATIONS_DIR/scskiller-kde.desktop"
    if [[ -e "$DESKTOP_TARGET" ]]; then
        if [[ ! -f "$DESKTOP_TARGET" ]] ||
           ! grep -q '^Name=SCSKiller    python3 - "$DEST/share/applications/scskiller-kde.desktop" \
        "$APPLICATIONS_DIR/scskiller-kde.desktop" "$DEST/bin/scskiller-kde" <<'PY'
import pathlib
import sys

source, target, executable = map(pathlib.Path, sys.argv[1:])
desktop = source.read_text(encoding="utf-8")
escaped = str(executable).replace("\\", "\\\\").replace('"', '\\"')
lines = desktop.splitlines()
for i, line in enumerate(lines):
    if line == "Exec=scskiller-kde":
        lines[i] = f'Exec="{escaped}"'
target.write_text("\n".join(lines) + "\n", encoding="utf-8")
PY
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database "$APPLICATIONS_DIR" >/dev/null 2>&1 || true
    fi
    printf 'Installed KDE launcher: %s\n' "$APPLICATIONS_DIR/scskiller-kde.desktop"
fi

printf 'Installed SCSKiller to: %s\n' "$DEST"
printf 'CLI symlink: %s\n' "$CLI_LINK"
printf 'Run: %s help\n' "$CLI_LINK"
printf 'The package requires a working Vulkan loader and GPU driver/ICD.\n'
 "$DESKTOP_TARGET" ||
           ! grep -q 'scskiller-kde' "$DESKTOP_TARGET"; then
            printf 'Refusing to replace an unrelated desktop entry: %s\n' "$DESKTOP_TARGET" >&2
            exit 2
        fi
    fi
fi

mkdir -p -- "$DEST" "$BIN_DIR"
cp -a -- "$SOURCE/." "$DEST/"

ln -sfn -- "$DEST/bin/scskiller-linux" "$CLI_LINK"

if [[ -x "$DEST/bin/scskiller-kde" && -f "$DEST/share/applications/scskiller-kde.desktop" ]]; then
    mkdir -p -- "$APPLICATIONS_DIR"
    python3 - "$DEST/share/applications/scskiller-kde.desktop" \
        "$APPLICATIONS_DIR/scskiller-kde.desktop" "$DEST/bin/scskiller-kde" <<'PY'
import pathlib
import sys

source, target, executable = map(pathlib.Path, sys.argv[1:])
desktop = source.read_text(encoding="utf-8")
escaped = str(executable).replace("\\", "\\\\").replace('"', '\\"')
lines = desktop.splitlines()
for i, line in enumerate(lines):
    if line == "Exec=scskiller-kde":
        lines[i] = f'Exec="{escaped}"'
target.write_text("\n".join(lines) + "\n", encoding="utf-8")
PY
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database "$APPLICATIONS_DIR" >/dev/null 2>&1 || true
    fi
    printf 'Installed KDE launcher: %s\n' "$APPLICATIONS_DIR/scskiller-kde.desktop"
fi

printf 'Installed SCSKiller to: %s\n' "$DEST"
printf 'CLI symlink: %s\n' "$CLI_LINK"
printf 'Run: %s help\n' "$CLI_LINK"
printf 'The package requires a working Vulkan loader and GPU driver/ICD.\n'
