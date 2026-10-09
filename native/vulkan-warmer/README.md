# SCSKiller Vulkan warmer

scskiller-vulkan-warmer consumes an SCSKiller Vulkan JSONL recording and replays the reconstructible subset through a real Vulkan driver.

Current replay support:
- SPIR-V shader modules captured by shader_module_code.
- Descriptor-set layouts with no unsupported pNext state or immutable samplers.
- Pipeline layouts whose pNext state and referenced descriptor-set layouts are fully represented in the recording.
- Compute pipelines, including entry-point names, pipeline flags, and specialization constants.
- Classic render passes without unsupported pNext state.
- Graphics pipelines using the recorded shader stages and fixed-function state for classic render passes.
- Vulkan 1.3 dynamic-rendering graphics pipelines when the selected device exposes the required dynamicRendering feature.
- Optional input driver-owned VkPipelineCache blobs.
- Optional output driver-owned VkPipelineCache snapshots.

Unsupported descriptor/pipeline layout extensions propagate to dependent pipelines; those pipelines are skipped rather than replayed with a different resource interface.

Current graphics limitations:
- Dynamic-rendering pipelines that require unrecorded extension pNext state are skipped.
- Graphics pipelines with unsupported pNext state are skipped.
- Graphics pipeline derivatives using basePipelineIndex are skipped until capture/replay can preserve their creation batch relationships.
- Extension-specific state not serialized by the recorder is intentionally not guessed.

The tool never parses or edits vendor cache internals. Driver cache blobs remain opaque and are only passed back to Vulkan on matching hardware/software stacks.
