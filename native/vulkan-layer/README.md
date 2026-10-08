# Experimental Vulkan runtime layer

VK_LAYER_SCSKILLER is the Linux runtime capture point for SCSKiller.

Intended paths:

- native Vulkan game -> SCSKiller Vulkan layer
- D3D12 game -> vkd3d-proton -> SCSKiller Vulkan layer
- D3D8/9/10/11 game -> DXVK -> SCSKiller Vulkan layer

The layer must not read or mutate NVIDIA/Mesa driver cache files directly. It records Vulkan-level shader and pipeline inputs and lets the Vulkan implementation own its cache.

The current native source is loader-integration scaffolding only. Pipeline interception is the next implementation step.
