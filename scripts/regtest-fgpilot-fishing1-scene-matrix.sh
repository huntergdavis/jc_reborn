#!/bin/bash
# Run the exact visible-window fishing fgpilot check for multiple fgpilot scenes
# and collect the per-scene summary results in one place.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

OUT_DIR="${1:-/tmp/fgpilot-fishing1-scene-matrix}"
SCENES="${SCENES:-testcard fishing1 fishing1raw adsfishing1}"
FRAMES="${FRAMES:-1320}"
START_FRAME="${START_FRAME:-1230}"
INTERVAL="${INTERVAL:-30}"

SHARED_DIR="$OUT_DIR/shared"
BASE_DIR="$SHARED_DIR/base"
OVERLAY_ONLY_DIR="$SHARED_DIR/overlay-only"

mkdir -p "$OUT_DIR"

"$SCRIPT_DIR/regtest-scene.sh" \
    --scene "FISHING 1" \
    --frames "$FRAMES" \
    --start-frame "$START_FRAME" \
    --interval "$INTERVAL" \
    --overlay-mask \
    --vram-write-dumps \
    --output "$BASE_DIR" >/dev/null

"$SCRIPT_DIR/regtest-scene.sh" \
    --scene "FISHING 1" \
    --boot "story scene 17" \
    --frames "$FRAMES" \
    --start-frame "$START_FRAME" \
    --interval "$INTERVAL" \
    --overlay \
    --vram-write-dumps \
    --output "$OVERLAY_ONLY_DIR" >/dev/null

for scene in $SCENES; do
    SCENE_DIR="$OUT_DIR/$scene"
    FGPILOT_DIR="$SCENE_DIR/fgpilot"
    FGPILOT_COMPARE_JSON="$SCENE_DIR/compare-fgpilot-vs-overlay.json"
    SUMMARY_JSON="$SCENE_DIR/summary.json"

    mkdir -p "$SCENE_DIR"

    "$SCRIPT_DIR/regtest-scene.sh" \
        --scene "FISHING 1" \
        --boot "story scene 17 fgoverlay ${scene}" \
        --frames "$FRAMES" \
        --start-frame "$START_FRAME" \
        --interval "$INTERVAL" \
        --overlay \
        --vram-write-dumps \
        --output "$FGPILOT_DIR" >/dev/null

    python3 "$SCRIPT_DIR/compare-regtest-result-bundles.py" \
        --base "$OVERLAY_ONLY_DIR/result.json" \
        --overlay "$FGPILOT_DIR/result.json" \
        > "$FGPILOT_COMPARE_JSON"

    python3 - <<'PY' "$BASE_DIR/result.json" "$OVERLAY_ONLY_DIR/result.json" "$FGPILOT_COMPARE_JSON" "$scene" > "$SUMMARY_JSON"
import json
import sys
from pathlib import Path

base_result = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
overlay_result = json.loads(Path(sys.argv[2]).read_text(encoding="utf-8"))
fgpilot_compare = json.loads(Path(sys.argv[3]).read_text(encoding="utf-8"))
scene = sys.argv[4]

def classify(compare):
    outcome = compare["outcome"]
    black = compare["filtered_frame_black_stats"]
    visible = compare["filtered_visible_frames"]
    base_all_black = black["base"]["all_black_count"] == black["base"]["frame_count"] and black["base"]["frame_count"] > 0
    overlay_all_black = black["overlay"]["all_black_count"] == black["overlay"]["frame_count"] and black["overlay"]["frame_count"] > 0
    base_has_black = black["base"]["all_black_count"] > 0
    overlay_has_black = black["overlay"]["all_black_count"] > 0
    visual_diff = not outcome["filtered_frames_equal"]
    visible_visual_diff = not visible["all_common_identical"]
    upload_diff = not outcome["cpu_to_vram_dumps_equal"]
    vram_diff = not outcome["vram_hash_equal"]
    state_diff = (not outcome["save_state_hash_equal"]) or (not outcome["ram_hash_equal"])
    return {
        "base_all_black": base_all_black,
        "overlay_all_black": overlay_all_black,
        "base_has_black": base_has_black,
        "overlay_has_black": overlay_has_black,
        "window_invalid_black": base_has_black or overlay_has_black,
        "visual_diff": visual_diff,
        "visible_frame_count": visible["common_count"],
        "visible_visual_diff": visible_visual_diff,
        "visible_first_diff": visible["first_diff"],
        "upload_diff": upload_diff,
        "vram_diff": vram_diff,
        "state_diff": state_diff,
        "nonvisual_state_only": state_diff and not visible_visual_diff and not upload_diff and not vram_diff,
    }

payload = {
    "fgoverlay_scene": scene,
    "shared_base_result": str(Path(sys.argv[1])),
    "shared_overlay_result": str(Path(sys.argv[2])),
    "fgpilot_vs_overlay": classify(fgpilot_compare),
    "current_hard_read": {
        "overlay_mode_alone_is_nonvisual_state_only": True,
        "fgpilot_adds_only_nonvisual_state_drift": classify(fgpilot_compare)["nonvisual_state_only"],
        "window_invalid_black": classify(fgpilot_compare)["window_invalid_black"],
        "any_visual_diff": classify(fgpilot_compare)["visual_diff"],
        "any_visible_subset_diff": classify(fgpilot_compare)["visible_visual_diff"],
        "any_upload_diff": classify(fgpilot_compare)["upload_diff"],
        "any_vram_diff": classify(fgpilot_compare)["vram_diff"],
    },
    "shared_overlay_reference": {
        "result_json": str(Path(sys.argv[2])),
        "state_hash": overlay_result["outcome"].get("state_hash"),
        "save_state_hash": overlay_result["outcome"].get("raw_hashes", {}).get("save_state_hash"),
        "ram_hash": overlay_result["outcome"].get("raw_hashes", {}).get("ram_hash"),
        "vram_hash": overlay_result["outcome"].get("raw_hashes", {}).get("vram_hash"),
    },
    "fgpilot_raw_hashes": fgpilot_compare["raw_hashes"]["overlay"],
}
print(json.dumps(payload, indent=2))
PY
done

python3 - <<'PY' "$OUT_DIR" $SCENES
import json
import sys
from pathlib import Path

out_dir = Path(sys.argv[1])
scenes = sys.argv[2:]
rows = []
save_state_hashes = {}
ram_hashes = {}
vram_hashes = {}
for scene in scenes:
    summary_path = out_dir / scene / "summary.json"
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    save_state_hashes[scene] = summary["fgpilot_raw_hashes"]["save_state_hash"]
    ram_hashes[scene] = summary["fgpilot_raw_hashes"]["ram_hash"]
    vram_hashes[scene] = summary["fgpilot_raw_hashes"]["vram_hash"]
    rows.append({
        "scene": scene,
        "summary_path": str(summary_path),
        "current_hard_read": summary["current_hard_read"],
        "fgpilot_vs_overlay": summary["fgpilot_vs_overlay"],
        "fgpilot_raw_hashes": summary["fgpilot_raw_hashes"],
    })

payload = {
    "shared_base_result": str((out_dir / "shared" / "base" / "result.json")),
    "shared_overlay_result": str((out_dir / "shared" / "overlay-only" / "result.json")),
    "scene_state_distinctness": {
        "distinct_save_state_hashes": len(set(save_state_hashes.values())),
        "distinct_ram_hashes": len(set(ram_hashes.values())),
        "distinct_vram_hashes": len(set(vram_hashes.values())),
        "save_state_hash_by_scene": save_state_hashes,
        "ram_hash_by_scene": ram_hashes,
        "vram_hash_by_scene": vram_hashes,
    },
    "scenes": rows,
}
print(json.dumps(payload, indent=2))
PY
