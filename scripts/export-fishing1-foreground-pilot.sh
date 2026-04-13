#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_DIR="${1:-$PROJECT_ROOT/host-results/fishing1-foreground-pilot}"
HOST_CAPTURE_DIR="$OUTPUT_DIR/host-capture"
ANALYSIS_JSON="$OUTPUT_DIR/foreground-analysis.json"

mkdir -p "$OUTPUT_DIR"

"$SCRIPT_DIR/capture-host-scene.sh" \
  --scene "FISHING 1" \
  --mode scene-default \
  --seed 1 \
  --interval 1 \
  --until-exit \
  --output "$HOST_CAPTURE_DIR" \
  --foreground-only

python3 "$SCRIPT_DIR/analyze-foreground-plates.py" \
  --frames-dir "$HOST_CAPTURE_DIR/frames" \
  --output-json "$ANALYSIS_JSON"

echo "$ANALYSIS_JSON"
