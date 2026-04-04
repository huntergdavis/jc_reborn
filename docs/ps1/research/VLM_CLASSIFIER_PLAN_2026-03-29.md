# VLM Classifier Plan

## Goal

Replace the current heuristic caption layer with a real multimodal model that can answer questions like:

- `Johnny is fishing off the right side of the dock.`
- `Mary is visible center-left on the beach.`
- `The frame is still title/ocean only; no character is visible.`

The output should be structured JSON, not free-form prose.

## Current Read

The existing `scripts/vision_classifier.py` pipeline is still useful for:

- nearest-reference retrieval
- family/scene confusion reporting
- coarse failure mode detection

But it is not a true semantic model. Its summaries are derived from foreground heuristics and reference templates, so it cannot reliably identify actors or actions.

## Chosen Runtime Direction

Primary target:

- `OpenVINO GenAI`
- `llmware/Qwen2.5-VL-3B-Instruct-ov-int4`

Why:

- CPU-only path
- packaged wheel exists for Python 3.12 on this machine
- OpenVINO exposes a direct VLM pipeline API
- the model is already converted to OpenVINO and quantized for low-resource inference

Fallbacks if memory/runtime is still too heavy:

- a smaller OpenVINO-converted VLM, if available
- `llama.cpp` GGUF path with a smaller multimodal model

## Architecture

### 1. Keep the reference bank

The existing reference bank is still valuable as retrieval context.

For a query frame:

1. compute nearest reference matches with the bank
2. pass those matches into the VLM prompt
3. ask the VLM for strict JSON

This constrains the model without forcing it to invent semantics from scratch.

### 2. New VLM analyzer

Implemented in:

- `scripts/vision_vlm.py`

Responsibilities:

- load a real VLM
- load an image
- optionally load nearest reference hints from the bank
- prompt for structured semantics
- write machine-readable JSON
- render a review HTML page for sampled frames

### 3. Output schema

Target JSON keys:

- `screen_type`
- `summary`
- `characters`
- `objects`
- `actions`
- `confidence`
- `notes`

Each character should include:

- `name`
- `confidence`
- `position`
- `action`

## Immediate Next Steps

1. Install runtime:
   - `scripts/setup-vision-vlm-openvino.sh`
2. Download model:
   - `llmware/Qwen2.5-VL-3B-Instruct-ov-int4`
3. Run image smoke tests on hand-picked reference frames.
4. Compare captions across strongly distinct scenes:
   - `FISHING-1`
   - `BUILDING-2`
   - `ACTIVITY-4`
   - `MARY-1`
5. If quality is acceptable, add sampled-frame VLM analysis for full reference scenes.

## Success Criteria

The VLM path is only acceptable if it can reliably separate at least:

- title vs ocean vs live scene
- Johnny vs Mary vs no clear character
- fishing vs bathing vs standing vs walking when the frame is visually clear
- wrong-family failures in PS1 runs

If it cannot do that, the runtime/model pair should be replaced rather than tuned around.
