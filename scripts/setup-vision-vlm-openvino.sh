#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VENV_DIR="${1:-$ROOT/.venv-vlm}"

python3 -m venv "$VENV_DIR"
. "$VENV_DIR/bin/activate"

python -m pip install --upgrade pip
python -m pip install openvino-genai==2026.0.0.0 huggingface_hub pillow

cat <<EOF
VLM runtime installed into:
  $VENV_DIR

Next steps:
  . "$VENV_DIR/bin/activate"
  hf download llmware/Qwen2.5-VL-3B-Instruct-ov-int4 --local-dir "$ROOT/models/Qwen2.5-VL-3B-Instruct-ov-int4"
  python "$ROOT/scripts/vision_vlm.py" analyze-image \\
    --model-dir "$ROOT/models/Qwen2.5-VL-3B-Instruct-ov-int4" \\
    --image "$ROOT/regtest-references/FISHING-1/frames/frame_00004.bmp" \\
    --out-json "$ROOT/vision-artifacts/vlm-smoke/analysis.json" \\
    --bank-dir "$ROOT/vision-artifacts/vision-reference-pipeline-current/reference-bank" \\
    --scene-id FISHING-1
EOF
