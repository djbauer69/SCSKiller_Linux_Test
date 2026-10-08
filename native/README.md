# Native Linux components

## Vulkan layer

Build vulkan-layer/ with CMake and install the layer library plus layer.json into a Vulkan layer search path.

## Vulkan probe

vulkan-probe/ is a tiny loader/device sanity check. Run it before enabling SCSKiller recording; if it cannot create a Vulkan instance, the problem is below SCSKiller.

## Proton

The Proton warmer will be added after the portable recording format can reconstruct pipeline state. The existing Windows PE warmer remains the compatibility fallback for Proton games.