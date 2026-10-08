# Vulkan recording format

The runtime recorder emits newline-delimited JSON (JSONL). Each event has a monotonically increasing sequence and currently uses schema versions 1 and 2.

Current event types:

- shader_module_create
- graphics_pipeline_create
- graphics_pipeline_state
- compute_pipeline_create
- ray_tracing_pipeline_create
- descriptor_set_layout_create
- pipeline_layout_create
- compute_pipeline_state
- pipeline_cache_snapshot
- pipeline_cache_replay

A graphics pipeline state event records the shader-module hashes, specialization fingerprints, pipeline-layout hash, flags, and subpass. Compute pipeline state records the compute shader hash, specialization fingerprint, pipeline-layout hash, and flags. Descriptor-set and pipeline-layout events establish stable capture-time identities for replay. This is still an incremental reconstruction format: immutable sampler state and the broader graphics state block are not yet fully serialized.

## Driver-owned pipeline cache snapshots

When the application calls vkGetPipelineCacheData, the layer can persist the returned driver-owned cache blob beside the JSONL file:

    capture.jsonl
    capture.jsonl.cache.42.bin

The JSONL event points at the binary snapshot. The cache remains opaque; SCSKiller does not parse or modify vendor cache internals.

A later run can seed an empty VkPipelineCache by setting:

    SCSKILLER_VK_REPLAY_CACHE=/path/to/capture.jsonl.cache.42.bin

The layer only injects the snapshot when the application's own VkPipelineCacheCreateInfo has initialDataSize == 0. If the application supplies its own initial cache, it is left untouched.

This is the first practical Linux warming path: replay the driver's own cache rather than attempting to manufacture or edit NVIDIA/Mesa cache files.

## Environment variables

- SCSKILLER_VK_RECORD=1 enables recording.
- SCSKILLER_VK_RECORD_FILE=/path/file.jsonl selects the recording file.
- SCSKILLER_VK_REPLAY_CACHE=/path/cache.bin seeds empty Vulkan pipeline caches from a previously recorded snapshot.

The cache blob is device/driver-specific. A replay failure must be treated as a normal cache miss; it must never be assumed portable between different GPUs, drivers, or driver builds.

## Example

    {"schema":1,"event":"shader_module_create","sequence":1,"code_words":384,"hash":"..."}
    {"schema":2,"event":"shader_module_code","sequence":1,"hash":"...","code_base64":"..."}
    {"schema":2,"event":"descriptor_set_layout_create","sequence":2,"hash":"...","flags":0,"bindings":[]}
    {"schema":2,"event":"pipeline_layout_create","sequence":3,"hash":"...","flags":0,"set_layouts":["..."],"push_constants":[]}
    {"schema":2,"event":"compute_pipeline_state","sequence":4,"count":1,"pipelines":[{"layout_hash":"...","module_hash":"...","stage_flags":0,"stage":"compute","entry_point":"main","specialization":null,"flags":0}]}
    {"schema":1,"event":"pipeline_cache_snapshot","sequence":42,"size":123456,"path":"capture.jsonl.cache.42.bin"}

This format is still experimental. Compute pipelines are now replayable for the subset whose descriptor layouts and pipeline layouts are fully represented and whose state does not depend on unrecorded pNext objects or immutable sampler reconstruction. The long-term graphics format still needs vertex input, input assembly, tessellation, viewport/scissor, rasterization, multisample, depth/stencil, color blend, render-pass/dynamic-rendering, and related pNext state before arbitrary graphics pipelines can be reconstructed without vendor cache files.


### render_pass_create

Legacy render-pass captures serialize attachment descriptions, subpass attachment references, preserve lists, dependencies, and a replay-compatibility flag. Unsupported pNext state marks the render pass as non-replayable rather than silently approximating it.
