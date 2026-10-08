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

The native Vulkan warmer can reconstruct recorded compute pipelines when the capture contains reconstructible descriptor-set layouts and pipeline layouts. CI exercises this path with a real GLSL compute shader, Vulkan pipeline creation, JSONL capture, and a second Vulkan process that rebuilds the compute pipeline and writes a driver-owned pipeline-cache blob.

Graphics pipeline replay remains deliberately gated on fuller fixed-function and rendering-state capture.


## Capturing a Proton game

The managed Linux CLI can launch a Windows game through Proton with the SCSKiller Vulkan layer enabled:

    scskiller-linux record-proton <proton> <compatdata> <workdir> <game-exe> <layer-directory> <capture.jsonl> [game arguments...]

The recorder sets STEAM_COMPAT_DATA_PATH, enables VK_LAYER_SCSKILLER through VK_INSTANCE_LAYERS, prepends the supplied layer directory to VK_LAYER_PATH, and writes SCSKILLER_VK_RECORD_FILE to the requested capture. This works at the common Vulkan runtime boundary, so D3D9-11 through DXVK and D3D12 through vkd3d-proton can be captured by the same layer.

The capture remains diagnostic: unsupported Vulkan extension pNext state is marked as non-replayable rather than guessed.
