# Vulkan recording format

The runtime recorder emits newline-delimited JSON (JSONL). Events carry a sequence counter for diagnostics and for pairing related records (for example shader creation with its SPIR-V bytes). Counters are process-local, so multiple processes may reuse values and concurrent appends need not be sorted by sequence. Consumers should use event contents and stable hashes for correlation rather than assuming a globally unique, file-ordered counter. Schema versions are currently 1, 2, and 3.

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
- pipeline_cache_replay_skipped
- physical_device_identity

## Replayable state

Shader-module code is stored as Base64 SPIR-V so the standalone Vulkan warmer can recreate shader modules without access to the original game process.

A physical_device_identity event records the Vulkan vendor/device IDs, driver version, API version, device name, and pipeline-cache UUID observed by the application. JSONL append events are protected by both an in-process mutex and an OS file lock so concurrent game processes can safely share the same recording path. The warmer uses the vendor/device IDs and UUID to prefer the corresponding physical device when multiple Vulkan devices are present.

Descriptor-set layouts and pipeline layouts are serialized using capture-stable hashes and their core state, with explicit replay-compatibility flags. Any unrecorded pNext state marks that layout non-replayable; compatibility propagates from descriptor-set layouts to pipeline layouts so dependent pipelines are skipped rather than compiled against an approximate interface. Immutable sampler bindings are likewise rejected because sampler objects are not reconstructed yet.

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

When the application calls vkGetPipelineCacheData, the layer can persist the returned driver-owned cache blob beside the JSONL file. Snapshot names include the writer process ID and event sequence so multiple game processes do not overwrite each other's cache files:

    capture.jsonl
    capture.jsonl.cache.12345.42.bin

The JSONL event points at the binary snapshot. The cache remains opaque; SCSKiller does not parse or modify vendor cache internals.

A later run can seed an empty VkPipelineCache by passing the snapshot to the standalone warmer, or by setting:

    SCSKILLER_VK_REPLAY_CACHE=/path/to/capture.jsonl.cache.42.bin

The layer only injects the snapshot when the application's own VkPipelineCacheCreateInfo has initialDataSize == 0. If the application supplies its own initial cache, it is left untouched.

The cache blob is device/driver-specific. Before injection, the layer validates the standard cache header against the selected physical device's vendor ID, device ID, and pipeline-cache UUID, and skips injection if the header or cache-creation pNext chain is unsupported. A pipeline_cache_replay_skipped event marks that fallback; the application's own pipeline-cache creation then proceeds with an empty cache. Never assume these blobs are portable between different GPUs, drivers, or driver builds.

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
