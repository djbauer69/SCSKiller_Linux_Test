# Proton warm helper

The eventual Proton warmer will execute the existing PE warm path inside the game's Proton/Wine environment where that is the most reliable way to reproduce D3D12 pipeline creation.

Linux-native Vulkan games will use a native Vulkan warmer.

This directory deliberately contains no Windows d3d12.dll proxy. The common recorder is the Vulkan layer.
