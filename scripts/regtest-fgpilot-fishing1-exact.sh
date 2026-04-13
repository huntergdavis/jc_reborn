#!/bin/bash
# Run the exact late-window FISHING 1 fgpilot A/B on the current head and
# compare baseline vs overlay at the filtered-frame and CPU->VRAM dump level.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

OUT_DIR="${1:-/tmp/fgpilot-fishing1-exact}"
FRAMES="${FRAMES:-1320}"
START_FRAME="${START_FRAME:-1170}"
INTERVAL="${INTERVAL:-30}"

BASE_DIR="$OUT_DIR/base"
OVERLAY_DIR="$OUT_DIR/overlay"
COMPARE_JSON="$OUT_DIR/compare.json"

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
    --boot "story scene 17 fgoverlay testcard" \
    --frames "$FRAMES" \
    --start-frame "$START_FRAME" \
    --interval "$INTERVAL" \
    --overlay \
    --vram-write-dumps \
    --output "$OVERLAY_DIR"

python3 "$SCRIPT_DIR/compare-regtest-result-bundles.py" \
    --base "$BASE_DIR/result.json" \
    --overlay "$OVERLAY_DIR/result.json" \
    > "$COMPARE_JSON"

cat "$COMPARE_JSON"
