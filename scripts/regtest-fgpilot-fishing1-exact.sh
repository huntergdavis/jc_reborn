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

printf '=== overlay vs mask ===\n'
cat "$OVERLAY_COMPARE_JSON"
printf '\n=== fgpilot vs overlay ===\n'
cat "$FGPILOT_COMPARE_JSON"
