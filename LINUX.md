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
4. DXVK/vkd3d-proton runtime detection and Proton orchestration.
5. Planner adapters for DXBC/DXIL/SPIR-V.
6. Native Linux CLI.
7. Qt 6/Kirigami KDE UI.
8. Linux CI and hardware validation.

The existing Windows D3D12 proxy remains unchanged.
