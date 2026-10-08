# SCSKiller Vulkan warmer

scskiller-vulkan-warmer consumes an SCSKiller Vulkan JSONL recording and replays the reconstructible subset through a real Vulkan driver.

Current replay support:
- SPIR-V shader modules captured by shader_module_code.
- Descriptor-set layouts whose bindings do not require immutable samplers.
- Pipeline layouts containing recorded descriptor-set layouts and push-constant ranges.
- Compute pipelines, including entry-point names, pipeline flags, and specialization constants.
- Classic render passes without unsupported pNext state.
- Graphics pipelines using the recorded shader stages and fixed-function state for classic render passes.
- Vulkan 1.3 dynamic-rendering graphics pipelines when the selected device exposes the required dynamicRendering feature.
- Optional input driver-owned VkPipelineCache blobs.
- Optional output driver-owned VkPipelineCache snapshots.

Current graphics limitations:
- Dynamic-rendering pipelines that require unrecorded extension pNext state are skipped.
- Graphics pipelines with unsupported pNext state are skipped.
- Graphics pipeline derivatives using basePipelineIndex are skipped until capture/replay can preserve their creation batch relationships.
- Extension-specific state not serialized by the recorder is intentionally not guessed.

The tool never parses or edits vendor cache internals. Driver cache blobs remain opaque and are only passed back to Vulkan on matching hardware/software stacks.
