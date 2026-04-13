#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_DIR="${1:-$PROJECT_ROOT/host-results/fishing1-foreground-pilot}"
HOST_CAPTURE_DIR="$OUTPUT_DIR/host-capture"
ANALYSIS_JSON="$OUTPUT_DIR/foreground-analysis.json"
PACK_PATH="$PROJECT_ROOT/generated/ps1/foreground/FISHING1.FG1"
PACK_JSON="$OUTPUT_DIR/foreground-pack.json"

mkdir -p "$OUTPUT_DIR"

"$SCRIPT_DIR/capture-host-scene.sh" \
  --scene "FISHING 1" \
  --mode scene-default \
  --seed 1 \
  --start-frame 0 \
  --interval 1 \
  --until-exit \
  --no-stamp \
  --output "$HOST_CAPTURE_DIR" \
  --foreground-only

python3 "$SCRIPT_DIR/analyze-foreground-plates.py" \
  --frames-dir "$HOST_CAPTURE_DIR/frames" \
  --output-json "$ANALYSIS_JSON"

python3 "$SCRIPT_DIR/build-fishing1-foreground-pack.py" \
  --frames-dir "$HOST_CAPTURE_DIR/frames" \
  --output-pack "$PACK_PATH" \
  --output-json "$PACK_JSON" \
  --frame-step 6

echo "$ANALYSIS_JSON"
echo "$PACK_JSON"
echo "$PACK_PATH"
