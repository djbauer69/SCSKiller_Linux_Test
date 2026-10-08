# Vulkan recording format

The runtime recorder emits newline-delimited JSON (JSONL). Each event has a monotonically increasing sequence.

Current event types:

- shader_module_create
- graphics_pipeline_create
- compute_pipeline_create

Example:

{"event":"shader_module_create","sequence":1,"code_words":384,"hash":"..."}
{"event":"graphics_pipeline_create","sequence":2,"count":3}

This is intentionally a diagnostic/intermediate format. It is not yet a replay/cache format.

Environment variables:

- SCSKILLER_VK_RECORD=1 enables recording.
- SCSKILLER_VK_RECORD_FILE=/path/file.jsonl selects the output.

The eventual portable recording format will add stable shader identifiers and Vulkan state needed to reconstruct pipeline creation without depending on vendor cache files.