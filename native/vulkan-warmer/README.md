# SCSKiller Vulkan warmer

scskiller-vulkan-warmer consumes an SCSKiller Vulkan JSONL recording and replays the currently reconstructible subset through a real Vulkan driver.

Current replay support:
- SPIR-V shader modules captured by shader_module_code.
- Descriptor-set layouts whose bindings do not require immutable samplers.
- Pipeline layouts containing recorded descriptor-set layouts and push-constant ranges.
- Compute pipelines, including entry-point names, pipeline flags, and specialization constants.
- Optional input driver-owned VkPipelineCache blobs.
- Optional output driver-owned VkPipelineCache snapshots.

Graphics and ray-tracing pipelines are intentionally skipped until their complete state is serialized. The tool never parses or edits vendor cache internals.
