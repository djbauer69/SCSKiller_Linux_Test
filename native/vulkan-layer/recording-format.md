# Vulkan recording format

The runtime recorder emits newline-delimited JSON (JSONL). Each event has a monotonically increasing sequence. Schema versions are currently 1, 2, and 3.

Current event types:

- shader_module_create
- shader_module_code
- graphics_pipeline_create
- graphics_pipeline_state
- compute_pipeline_create
- compute_pipeline_state
- ray_tracing_pipeline_create
- descriptor_set_layout_create
- pipeline_layout_create
- render_pass_create
- pipeline_cache_snapshot
- pipeline_cache_replay
- physical_device_identity

## Replayable state

Shader-module code is stored as Base64 SPIR-V so the standalone Vulkan warmer can recreate shader modules without access to the original game process.

A physical_device_identity event records the Vulkan vendor/device IDs, driver version, API version, device name, and pipeline-cache UUID observed by the application. The warmer uses the vendor/device IDs and UUID to prefer the corresponding physical device when multiple Vulkan devices are present.

Descriptor-set layouts and pipeline layouts are serialized using capture-stable hashes and their core state. Immutable sampler bindings are recorded but are currently rejected by the standalone warmer because sampler objects are not reconstructed yet.

Legacy render passes record attachment descriptions, subpass attachment references, preserve lists, dependencies, and a replay-compatibility flag. Unsupported render-pass pNext state marks the render pass non-replayable instead of silently approximating it.

Graphics pipeline state records:

- shader stages, module hashes, entry points, and specialization data
- pipeline-layout and render-pass hashes
- pipeline flags, subpass, and derivative information
- vertex input bindings and attributes
- input assembly
- tessellation
- viewport/scissor state
- rasterization
- multisampling and sample masks
- depth/stencil state
- color blend state
- dynamic states
- dynamic-rendering formats
- pNext compatibility markers for the captured state blocks

The standalone warmer replays classic render-pass graphics pipelines and Vulkan 1.3 dynamic-rendering graphics pipelines whose core state is fully reconstructible from this recording. Dynamic-rendering replay requires the selected physical device to expose and enable the core dynamicRendering feature. Pipelines with unrecorded extension pNext state and graphics pipeline derivatives using basePipelineIndex are still skipped.

Compute pipeline state records the compute shader hash, specialization data, pipeline-layout hash, stage flags, and pipeline flags.

## Driver-owned pipeline cache snapshots

When the application calls vkGetPipelineCacheData, the layer can persist the returned driver-owned cache blob beside the JSONL file:

    capture.jsonl
    capture.jsonl.cache.42.bin

The JSONL event points at the binary snapshot. The cache remains opaque; SCSKiller does not parse or modify vendor cache internals.

A later run can seed an empty VkPipelineCache by passing the snapshot to the standalone warmer, or by setting:

    SCSKILLER_VK_REPLAY_CACHE=/path/to/capture.jsonl.cache.42.bin

The layer only injects the snapshot when the application's own VkPipelineCacheCreateInfo has initialDataSize == 0. If the application supplies its own initial cache, it is left untouched.

The cache blob is device/driver-specific. A replay failure must be treated as a normal cache miss; it must never be assumed portable between different GPUs, drivers, or driver builds.

## Environment variables

- SCSKILLER_VK_RECORD=1 enables recording.
- SCSKILLER_VK_RECORD_FILE=/path/file.jsonl selects the recording file.
- SCSKILLER_VK_REPLAY_CACHE=/path/cache.bin seeds empty Vulkan pipeline caches from a previously recorded snapshot.
- SCSKILLER_VK_DEBUG=1 enables diagnostic messages from the Vulkan layer.

## Example

    {"schema":2,"event":"shader_module_code","sequence":1,"hash":"...","code_base64":"..."}
    {"schema":3,"event":"descriptor_set_layout_create","sequence":2,"hash":"...","flags":0,"bindings":[]}
    {"schema":3,"event":"pipeline_layout_create","sequence":3,"hash":"...","flags":0,"set_layouts":["..."],"push_constants":[]}
    {"schema":3,"event":"render_pass_create","sequence":4,"hash":"...","flags":0,"replay_compatible":true,...}
    {"schema":3,"event":"graphics_pipeline_state","sequence":5,"count":1,"pipelines":[{"layout_hash":"...","stage_count":2,...}]}
    {"schema":3,"event":"compute_pipeline_state","sequence":6,"count":1,"pipelines":[{"layout_hash":"...","module_hash":"...","stage_flags":0,"entry_point":"main","specialization":null,"flags":0}]}
    {"schema":1,"event":"pipeline_cache_snapshot","sequence":42,"size":123456,"path":"capture.jsonl.cache.42.bin"}

This format remains intentionally incremental. The guiding rule is to serialize enough real Vulkan state to recreate the pipeline through vkCreate*Pipelines, while refusing to guess unsupported extension state.
