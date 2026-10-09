# CachyOS / Arch Linux package

This is an experimental VCS-style `PKGBUILD` for the `linux-vulkan-proton` branch. It builds the Vulkan layer and warmer, publishes a self-contained `linux-x64` CLI, builds the Qt/Kirigami UI, and installs the bundle under `/opt/scskiller-linux` with CLI/UI launchers and a desktop entry.

Install build dependencies first:

```bash
sudo pacman -S --needed \
  base-devel git cmake ninja dotnet-sdk-10.0 vulkan-headers \
  vulkan-icd-loader qt6-base qt6-declarative qt6-shadertools kirigami \
  icu openssl zlib libunwind
```

Then build and install from this directory:

```bash
cd packaging/arch
makepkg -si
```

The package provides:

- `/usr/bin/scskiller-linux`
- `/usr/bin/scskiller-kde`
- `/usr/share/applications/scskiller-kde.desktop`
- `/opt/scskiller-linux/` with the Vulkan layer, native warmer, managed CLI, and docs

Use `scskiller-linux help` for commands. The GUI provides capture, inspect, compatibility planning, warming, and native/Proton launch controls.

This does not install a GPU driver/ICD or replace Steam/Proton. You still need a working system Vulkan loader and GPU driver. The port is experimental; validate a real game's capture and strict replay plan before relying on it.
