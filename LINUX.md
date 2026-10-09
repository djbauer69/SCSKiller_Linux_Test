# Experimental Linux architecture

This branch is the experimental Linux implementation of SCSKiller.

## Graphics paths

| Game API | Linux path |
| --- | --- |
| Vulkan | Native Vulkan |
| D3D12 | vkd3d-proton -> Vulkan |
| D3D11 | DXVK -> Vulkan |
| D3D10 | DXVK -> Vulkan |
| D3D9 | DXVK -> Vulkan |
| D3D8 | DXVK -> Vulkan |

The Vulkan layer is the common runtime recording point. This avoids maintaining separate pipeline recorders for native Vulkan, DXVK, and vkd3d-proton.

vkd3d-proton owns its shader/pipeline cache, and DXVK maintains its own shader/state caches. SCSKiller should feed these runtimes through real Vulkan pipeline creation rather than editing cache files.

## Implementation stages

1. Cross-platform graphics contracts and Vulkan loader layer.
2. Dispatch-safe Vulkan pipeline interception and portable recording format.
3. Vulkan warmer consuming recorded pipeline descriptions.
4. DXVK/vkd3d-proton runtime detection and Proton orchestration, including Vulkan-layer recording through Proton.
5. Planner adapters for DXBC/DXIL/SPIR-V.
6. Native Linux CLI.
7. Qt 6/Kirigami KDE UI.
8. Linux CI and hardware validation.

The existing Windows D3D12 proxy remains unchanged.


## Current replay milestone

The standalone Vulkan warmer reconstructs and submits captured compute pipelines, classic render-pass graphics pipelines, and Vulkan 1.3 dynamic-rendering graphics pipelines to a real Vulkan driver when the recording includes the required state. CI asserts that both classic and dynamic graphics fixtures actually compile with zero skips; it also exercises driver-cache export and injection, native recording through the managed CLI, and a staged Linux package.

The JSONL recording stores SPIR-V shader bytes, pipeline/layout/rendering state, pNext replay-compatibility markers, and physical-device identity. It refuses to guess unknown extension state. Pipelines with immutable samplers, unsupported pNext chains, or derivative relationships that cannot be reconstructed are skipped. Driver-owned pipeline cache blobs remain opaque and should only be reused with a matching pipeline-cache UUID.

This is an experimental foundation, not yet proof that an arbitrary real game's entire pipeline set can be recreated. CI now exercises the package launcher, strict replay reporting, compatible and incompatible cache paths, device-group enumeration, concurrent-process capture, and actual compilation of the supported core pipeline fixtures. The remaining external validation is on a real CachyOS machine/GPU and with a real Proton title on each supported translation path; that hardware/game test has not yet been performed by CI.


## Capturing a Proton game

The managed Linux CLI can launch native Vulkan applications or Windows games through Proton with the SCSKiller Vulkan layer enabled:

    scskiller-linux record-proton <proton> <compatdata> <workdir> <game-exe> <layer-directory> <capture.jsonl> [game arguments...]

The recorder sets STEAM_COMPAT_DATA_PATH, enables VK_LAYER_SCSKILLER through VK_INSTANCE_LAYERS, prepends the supplied layer directory to VK_LAYER_PATH, and writes SCSKILLER_VK_RECORD_FILE to the requested capture. This works at the common Vulkan runtime boundary, so D3D9-11 through DXVK and D3D12 through vkd3d-proton can be captured by the same layer.

The capture remains diagnostic: unsupported Vulkan extension pNext state is marked as non-replayable rather than guessed.


For a native Vulkan executable:

    scskiller-linux record-vulkan <executable> <workdir> <layer-directory> <capture.jsonl> [arguments...]

For a Proton game:

    scskiller-linux record-proton <proton> <compatdata> <workdir> <game-exe> <layer-directory> <capture.jsonl> [game arguments...]

Both commands enable VK_LAYER_SCSKILLER and record at the common Vulkan boundary. This keeps native Vulkan, DXVK, and vkd3d-proton capture on the same recording format and replay path.


## Build on CachyOS / Arch Linux

Install a C++ toolchain and the package build dependencies, then build the staged distribution:

    sudo pacman -Syu
    sudo pacman -S --needed base-devel cmake ninja dotnet-sdk-10.0 vulkan-headers vulkan-icd-loader
    bash scripts/package-linux.sh

The SDK package provides .NET 10 for the managed CLI; Vulkan headers and the loader are used to build the layer and warmer. The graphics driver/ICD must also be installed for the user's GPU. The optional CI smoke-shader compiler is available in the `shaderc` package (`glslc`), but is not required by the packaging script itself.

## Build a local Linux package

On a development machine with CMake, Ninja, .NET 10 SDK, Vulkan development headers, and the Vulkan loader installed:

    bash scripts/package-linux.sh

This stages a self-contained directory layout at `dist/scskiller-linux`. The managed CLI is published self-contained for `linux-x64` or `linux-arm64`, so a separate .NET runtime is not required on the target machine. Set `BUILD_KDE_UI=1` to also compile and stage `scskiller-kde` with its desktop entry; this requires Qt 6 and KDE Kirigami at build time, and their runtime libraries on the target. The launcher is `dist/scskiller-linux/bin/scskiller-linux`; the Vulkan layer manifest, shared library, native warmer, and docs are staged alongside it. Override `STAGE` or `BUILD_ROOT` to choose other output directories.

Example capture command from the source checkout:

    dist/scskiller-linux/bin/scskiller-linux record-vulkan ./your-vulkan-app "$PWD" dist/scskiller-linux/share/vulkan/explicit_layer.d capture.jsonl

For Proton, pass the Proton executable, compatdata path, work directory, game executable, layer manifest directory, and capture path to `record-proton`. The recorders are experimental and should first be exercised with a small test application before using them with a full game.


Successful Linux CI runs also upload the staged directory as an artifact named `scskiller-linux-<commit-sha>` for 14 days. Download and extract it on a compatible Linux system, then run `bin/scskiller-linux help`. The package still requires a working Vulkan loader and an installed GPU driver/ICD; it does not bundle vendor drivers.


## Inspecting a capture

Before replaying a game capture, summarize the recorded GPU and the pipeline coverage:

    scskiller-linux inspect-vulkan capture.jsonl

The report lists captured SPIR-V modules, compute and graphics pipelines, dynamic-rendering pipelines, state marked incompatible, and cache replay events. It is a diagnostic count, not a guarantee that every pipeline from a full game is reconstructible; the warmer's compiled/skipped/failed totals remain the final check.

By default, `warm-vulkan` reports skipped pipelines but exits successfully if Vulkan itself completed without a pipeline-creation error. Add `--require-complete` when you need a strict pass/fail result: the command returns a nonzero status if any compute/graphics pipeline was skipped or failed, the output summary is missing, or ray-tracing pipelines remain unsupported.


## Warm and relaunch with the recorded Vulkan cache

A practical test cycle is capture, inspect, warm, then launch again with cache injection enabled. Use a second launch to exercise the cache warmed from the first run:

For a native Vulkan app:

    scskiller-linux record-vulkan ./game "$PWD" "$SCSKILLER_HOME/share/vulkan/explicit_layer.d" capture.jsonl
    scskiller-linux inspect-vulkan capture.jsonl
    scskiller-linux warm-vulkan capture.jsonl --output-cache warmed.cache.bin --require-complete
    scskiller-linux run-vulkan ./game "$PWD" "$SCSKILLER_HOME/share/vulkan/explicit_layer.d" warmed.cache.bin

For a Windows game launched through Proton:

    scskiller-linux record-proton /path/to/proton /path/to/compatdata /path/to/game-directory game.exe "$SCSKILLER_HOME/share/vulkan/explicit_layer.d" capture.jsonl
    scskiller-linux inspect-vulkan capture.jsonl
    scskiller-linux warm-vulkan capture.jsonl --output-cache warmed.cache.bin --require-complete
    scskiller-linux run-proton-vulkan /path/to/proton /path/to/compatdata /path/to/game-directory game.exe "$SCSKILLER_HOME/share/vulkan/explicit_layer.d" warmed.cache.bin

Replace the example executable, Proton, compatdata, and working-directory paths with those for the installed game. The warmed cache is created by the currently selected Vulkan driver and is not a portable shader archive. The layer injects it only when the app asks for an empty `VkPipelineCache`; a game-supplied non-empty cache remains untouched. If strict warming reports skipped pipelines, do not treat that cache as a complete warm-up. A real-game validation run should compare first-launch and subsequent-launch behavior and inspect the capture for unsupported state.
