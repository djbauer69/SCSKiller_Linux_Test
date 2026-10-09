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

This is an experimental foundation, not yet proof that an arbitrary real game's entire pipeline set can be recreated. The next integration gates are the package smoke test, then validation on the target CachyOS machine and with a real Proton game on each supported translation path.


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


## Build a local Linux package

On a development machine with CMake, Ninja, .NET 10 SDK, GLSL compiler, Vulkan development headers, and the Vulkan loader installed:

    bash scripts/package-linux.sh

This stages a self-contained directory layout at `dist/scskiller-linux` (the managed CLI uses the installed .NET 10 runtime). The launcher is `dist/scskiller-linux/bin/scskiller-linux`; the Vulkan layer manifest, shared library, native warmer, and docs are staged alongside it. Override `STAGE` or `BUILD_ROOT` to choose other output directories.

Example capture command from the source checkout:

    dist/scskiller-linux/bin/scskiller-linux record-vulkan ./your-vulkan-app "$PWD" dist/scskiller-linux/share/vulkan/explicit_layer.d capture.jsonl

For Proton, pass the Proton executable, compatdata path, work directory, game executable, layer manifest directory, and capture path to `record-proton`. The recorders are experimental and should first be exercised with a small test application before using them with a full game.


## Inspecting a capture

Before replaying a game capture, summarize the recorded GPU and the pipeline coverage:

    scskiller-linux inspect-vulkan capture.jsonl

The report lists captured SPIR-V modules, compute and graphics pipelines, dynamic-rendering pipelines, state marked incompatible, and cache replay events. It is a diagnostic count, not a guarantee that every pipeline from a full game is reconstructible; the warmer's compiled/skipped/failed totals remain the final check.

By default, `warm-vulkan` reports skipped pipelines but exits successfully if Vulkan itself completed without a pipeline-creation error. Add `--require-complete` when you need a strict pass/fail result: the command returns a nonzero status if any compute/graphics pipeline was skipped or failed, the output summary is missing, or ray-tracing pipelines remain unsupported.
