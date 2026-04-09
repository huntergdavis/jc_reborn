#!/bin/bash
# review-ps1-vs-reference.sh — Generate an HTML review page for one PS1 capture
# against its host reference, even when the aligner cannot verify a scene anchor.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

SCENE_SPEC=""
RESULT_PATH=""
REFERENCE_PATH=""
OUTPUT_HTML=""
COMPARE_JSON=""
VLM_MODEL_DIR=""
VLM_OUT_JSON=""
VLM_BANK_DIR=""
VLM_SAMPLES="3"
VLM_ENABLE=0
MIN_RESULT_SCENE_FRAME=""
MIN_REFERENCE_SCENE_FRAME="0"
SCENE_WINDOW_ONLY=0
TITLE=""

usage() {
    cat <<'USAGE'
Usage: review-ps1-vs-reference.sh [options]

Options:
  --scene "ADS TAG"           Scene label used in the HTML title
  --result PATH               PS1 result dir or result.json
  --reference PATH            Host reference dir or result.json
  --output PATH               Output HTML path
  --compare-json PATH         Optional compare JSON output path
  --vlm                       Enable local VLM scene-fix validation using auto-detected runtime/model if available
  --vlm-model-dir PATH        Optional OpenVINO VLM model dir for scene-fix verdict
  --vlm-out-json PATH         Optional VLM scene-fix JSON output path
  --vlm-bank-dir PATH         Optional reference-bank dir for VLM hints
  --vlm-samples N             Number of frame pairs to validate (default: 3)
  --min-result-scene-frame N  Minimum PS1 frame eligible as a scene-entry anchor
                              (default: reviewed per-scene start when --scene is set)
  --min-reference-scene-frame N
                              Minimum host frame eligible as a scene-entry anchor
  --scene-window-only         Clip comparison to the host scene window
  --title TEXT                Optional HTML title override
  -h, --help                  Show this help
USAGE
}

while [ $# -gt 0 ]; do
    case "$1" in
        --scene) SCENE_SPEC="$2"; shift 2 ;;
        --result) RESULT_PATH="$2"; shift 2 ;;
        --reference) REFERENCE_PATH="$2"; shift 2 ;;
        --output) OUTPUT_HTML="$2"; shift 2 ;;
        --compare-json) COMPARE_JSON="$2"; shift 2 ;;
        --vlm) VLM_ENABLE=1; shift ;;
        --vlm-model-dir) VLM_MODEL_DIR="$2"; shift 2 ;;
        --vlm-out-json) VLM_OUT_JSON="$2"; shift 2 ;;
        --vlm-bank-dir) VLM_BANK_DIR="$2"; shift 2 ;;
        --vlm-samples) VLM_SAMPLES="$2"; shift 2 ;;
        --min-result-scene-frame) MIN_RESULT_SCENE_FRAME="$2"; shift 2 ;;
        --min-reference-scene-frame) MIN_REFERENCE_SCENE_FRAME="$2"; shift 2 ;;
        --scene-window-only) SCENE_WINDOW_ONLY=1; shift ;;
        --title) TITLE="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if [ -z "$RESULT_PATH" ] || [ -z "$REFERENCE_PATH" ] || [ -z "$OUTPUT_HTML" ]; then
    echo "ERROR: --result, --reference, and --output are required." >&2
    usage
    exit 1
fi

if [ -z "$MIN_RESULT_SCENE_FRAME" ]; then
    if [ -n "$SCENE_SPEC" ]; then
        MIN_RESULT_SCENE_FRAME="$(python3 "$SCRIPT_DIR/get-scene-capture-start.py" --scene "$SCENE_SPEC")"
    else
        grace="${REGTEST_BOOT_GRACE_FRAMES:-1800}"
        tolerance="${REGTEST_BOOT_GRACE_TOLERANCE_FRAMES:-120}"
        MIN_RESULT_SCENE_FRAME=$((grace - tolerance))
        if [ "$MIN_RESULT_SCENE_FRAME" -lt 0 ]; then
            MIN_RESULT_SCENE_FRAME=0
        fi
    fi
fi

if [ -z "$COMPARE_JSON" ]; then
    COMPARE_JSON="${OUTPUT_HTML%.html}.json"
fi
if [ -z "$VLM_OUT_JSON" ]; then
    VLM_OUT_JSON="${OUTPUT_HTML%.html}-vlm.json"
fi
mkdir -p "$(dirname "$OUTPUT_HTML")"
mkdir -p "$(dirname "$COMPARE_JSON")"
mkdir -p "$(dirname "$VLM_OUT_JSON")"

if [ -z "$TITLE" ]; then
    if [ -n "$SCENE_SPEC" ]; then
        TITLE="$SCENE_SPEC PS1 vs Host Review"
    else
        TITLE="PS1 vs Host Review"
    fi
fi

COMPARE_CMD=(
    python3 "$SCRIPT_DIR/compare-sequence-runs.py"
    --json
    --scene-entry-align
    --result "$RESULT_PATH"
    --reference "$REFERENCE_PATH"
    --min-result-scene-frame "$MIN_RESULT_SCENE_FRAME"
    --min-reference-scene-frame "$MIN_REFERENCE_SCENE_FRAME"
)

if [ "$SCENE_WINDOW_ONLY" -eq 1 ]; then
    COMPARE_CMD+=(--scene-window-only)
fi

set +e
COMPARE_OUTPUT="$("${COMPARE_CMD[@]}" 2>&1)"
compare_rc=$?
set -e
if [ "$compare_rc" -eq 0 ]; then
    printf '%s\n' "$COMPARE_OUTPUT" > "$COMPARE_JSON"
else
    python3 - <<'PY' "$COMPARE_JSON" "$RESULT_PATH" "$REFERENCE_PATH" "$MIN_RESULT_SCENE_FRAME" "$MIN_REFERENCE_SCENE_FRAME" "$SCENE_SPEC" "$compare_rc" "$COMPARE_OUTPUT"
import json
import sys

compare_json = sys.argv[1]
result_path = sys.argv[2]
reference_path = sys.argv[3]
min_result = int(sys.argv[4]) if sys.argv[4] else None
min_reference = int(sys.argv[5]) if sys.argv[5] else None
scene_spec = sys.argv[6]
rc = int(sys.argv[7])
stderr = sys.argv[8]

payload = {
    "scene": scene_spec,
    "result": result_path,
    "reference": reference_path,
    "alignment_mode": "scene_entry",
    "frame_offset": None,
    "result_entry_frame": None,
    "reference_entry_frame": None,
    "min_result_scene_frame": min_result,
    "min_reference_scene_frame": min_reference,
    "common_frame_count": 0,
    "average_palette_index_diff_pixels": None,
    "result_only_frames": [],
    "reference_only_frames": [],
    "frames": [],
    "error": stderr or f"compare failed rc={rc}",
    "verdict": "COMPARE_FAILED",
}
with open(compare_json, "w", encoding="utf-8") as handle:
    json.dump(payload, handle, indent=2)
    handle.write("\n")
PY
fi

resolve_vlm_python() {
    if [ -x "$PROJECT_ROOT/.venv-vlm/bin/python" ]; then
        printf '%s\n' "$PROJECT_ROOT/.venv-vlm/bin/python"
        return 0
    fi
    command -v python3
}

if [ -n "$VLM_MODEL_DIR" ]; then
    VLM_ENABLE=1
fi

if [ -z "$VLM_BANK_DIR" ]; then
    VLM_PYTHON="$(resolve_vlm_python)"
    set +e
    VLM_BANK_DIR="$("$VLM_PYTHON" - <<'PY' "$PROJECT_ROOT" 2>/dev/null
import sys
from pathlib import Path

project_root = Path(sys.argv[1])
sys.path.insert(0, str(project_root / "scripts"))
import vision_vlm as vv

bank = vv.resolve_reference_bank(None)
if bank is not None:
    print(bank)
PY
)"
    rc=$?
    set -e
    if [ "$rc" -ne 0 ]; then
        VLM_BANK_DIR=""
    fi
fi

if [ "$VLM_ENABLE" -eq 1 ]; then
    VLM_PYTHON="${VLM_PYTHON:-$(resolve_vlm_python)}"
    VLM_CMD=(
        "$VLM_PYTHON" "$SCRIPT_DIR/validate-ps1-vlm.py"
        scene-fix
        --reference "$REFERENCE_PATH"
        --result "$RESULT_PATH"
        --out-json "$VLM_OUT_JSON"
        --samples "$VLM_SAMPLES"
    )
    if [ -n "$VLM_MODEL_DIR" ]; then
        VLM_CMD+=(--model-dir "$VLM_MODEL_DIR")
    fi
    if [ -n "$SCENE_SPEC" ]; then
        VLM_CMD+=(--scene-id "${SCENE_SPEC/ /-}")
    fi
    if [ -n "$VLM_BANK_DIR" ]; then
        VLM_CMD+=(--bank-dir "$VLM_BANK_DIR")
    fi
    "${VLM_CMD[@]}" >/dev/null
fi

RENDER_CMD=(
    python3 "$SCRIPT_DIR/render-compare-timeline.py"
    --compare-json "$COMPARE_JSON"
    --output "$OUTPUT_HTML"
    --title "$TITLE"
)
if [ "$VLM_ENABLE" -eq 1 ] && [ -f "$VLM_OUT_JSON" ]; then
    RENDER_CMD+=(--vlm-json "$VLM_OUT_JSON")
fi
"${RENDER_CMD[@]}" >/dev/null

echo "$OUTPUT_HTML"
