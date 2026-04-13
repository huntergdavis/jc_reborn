#!/bin/bash
# Run the exact late-window FISHING 1 fgpilot exact-window checks on the
# current head and compare:
# - baseline capture-overlay-mask
# - plain capture-overlay
# - capture-overlay + fgoverlay

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

OUT_DIR="${1:-/tmp/fgpilot-fishing1-exact}"
FRAMES="${FRAMES:-1320}"
START_FRAME="${START_FRAME:-1170}"
INTERVAL="${INTERVAL:-30}"

BASE_DIR="$OUT_DIR/base"
OVERLAY_ONLY_DIR="$OUT_DIR/overlay-only"
FGPILOT_DIR="$OUT_DIR/fgpilot"
OVERLAY_COMPARE_JSON="$OUT_DIR/compare-overlay-vs-mask.json"
FGPILOT_COMPARE_JSON="$OUT_DIR/compare-fgpilot-vs-overlay.json"
SUMMARY_JSON="$OUT_DIR/summary.json"

mkdir -p "$OUT_DIR"

"$SCRIPT_DIR/regtest-scene.sh" \
    --scene "FISHING 1" \
    --frames "$FRAMES" \
    --start-frame "$START_FRAME" \
    --interval "$INTERVAL" \
    --overlay-mask \
    --vram-write-dumps \
    --output "$BASE_DIR"

"$SCRIPT_DIR/regtest-scene.sh" \
    --scene "FISHING 1" \
    --boot "story scene 17" \
    --frames "$FRAMES" \
    --start-frame "$START_FRAME" \
    --interval "$INTERVAL" \
    --overlay \
    --vram-write-dumps \
    --output "$OVERLAY_ONLY_DIR"

"$SCRIPT_DIR/regtest-scene.sh" \
    --scene "FISHING 1" \
    --boot "story scene 17 fgoverlay testcard" \
    --frames "$FRAMES" \
    --start-frame "$START_FRAME" \
    --interval "$INTERVAL" \
    --overlay \
    --vram-write-dumps \
    --output "$FGPILOT_DIR"

python3 "$SCRIPT_DIR/compare-regtest-result-bundles.py" \
    --base "$BASE_DIR/result.json" \
    --overlay "$OVERLAY_ONLY_DIR/result.json" \
    > "$OVERLAY_COMPARE_JSON"

python3 "$SCRIPT_DIR/compare-regtest-result-bundles.py" \
    --base "$OVERLAY_ONLY_DIR/result.json" \
    --overlay "$FGPILOT_DIR/result.json" \
    > "$FGPILOT_COMPARE_JSON"

python3 - <<'PY' "$OVERLAY_COMPARE_JSON" "$FGPILOT_COMPARE_JSON" > "$SUMMARY_JSON"
import json
import sys
from pathlib import Path

overlay = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
fgpilot = json.loads(Path(sys.argv[2]).read_text(encoding="utf-8"))

def classify(compare):
    outcome = compare["outcome"]
    visual_diff = not outcome["filtered_frames_equal"]
    upload_diff = not outcome["cpu_to_vram_dumps_equal"]
    vram_diff = not outcome["vram_hash_equal"]
    state_diff = (not outcome["save_state_hash_equal"]) or (not outcome["ram_hash_equal"])
    return {
        "visual_diff": visual_diff,
        "upload_diff": upload_diff,
        "vram_diff": vram_diff,
        "state_diff": state_diff,
        "nonvisual_state_only": state_diff and not visual_diff and not upload_diff and not vram_diff,
    }

payload = {
    "overlay_vs_mask": classify(overlay),
    "fgpilot_vs_overlay": classify(fgpilot),
    "current_hard_read": {
        "overlay_mode_alone_is_nonvisual_state_only": classify(overlay)["nonvisual_state_only"],
        "fgpilot_adds_only_nonvisual_state_drift": classify(fgpilot)["nonvisual_state_only"],
        "any_visual_diff": classify(overlay)["visual_diff"] or classify(fgpilot)["visual_diff"],
        "any_upload_diff": classify(overlay)["upload_diff"] or classify(fgpilot)["upload_diff"],
        "any_vram_diff": classify(overlay)["vram_diff"] or classify(fgpilot)["vram_diff"],
    },
}
print(json.dumps(payload, indent=2))
PY

printf '=== overlay vs mask ===\n'
cat "$OVERLAY_COMPARE_JSON"
printf '\n=== fgpilot vs overlay ===\n'
cat "$FGPILOT_COMPARE_JSON"
printf '\n=== summary ===\n'
cat "$SUMMARY_JSON"
