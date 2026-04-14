#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_DIR="${1:-$PROJECT_ROOT/host-results/fishing1-foreground-pilot}"
HOST_CAPTURE_DIR="$OUTPUT_DIR/host-capture"
ANALYSIS_JSON="$OUTPUT_DIR/foreground-analysis.json"
PACK_PATH="$PROJECT_ROOT/generated/ps1/foreground/FISHING1.FG1"
PACK_JSON="$OUTPUT_DIR/foreground-pack.json"
DIRECT_PACK_PATH="$PROJECT_ROOT/generated/ps1/foreground/FISHING1D.FG1"
DIRECT_PACK_JSON="$OUTPUT_DIR/foreground-pack-direct.json"
RAW_FRAME_SOURCE="$HOST_CAPTURE_DIR/frames/frame_00024.bmp"
RAW_FRAME_PATH="$PROJECT_ROOT/generated/ps1/foreground/FISH24.RAW"
REFERENCE_META="$PROJECT_ROOT/regtest-references/FISHING-1/metadata.json"

if [ ! -f "$REFERENCE_META" ]; then
  echo "missing canonical reference metadata: $REFERENCE_META" >&2
  exit 1
fi

read -r CANONICAL_FRAME_COUNT CANONICAL_LAST_FRAME <<EOF
$(python3 - "$REFERENCE_META" <<'PY'
import json
import sys
from pathlib import Path

payload = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
frame_count = int(payload["frame_count"])
if frame_count <= 0:
    raise SystemExit("canonical frame_count must be > 0")
print(frame_count, frame_count - 1)
PY
)
EOF

mkdir -p "$OUTPUT_DIR"

"$SCRIPT_DIR/capture-host-scene.sh" \
  --scene "FISHING 1" \
  --mode scene-default \
  --seed 1 \
  --start-frame 0 \
  --interval 1 \
  --frames "$CANONICAL_LAST_FRAME" \
  --no-stamp \
  --output "$HOST_CAPTURE_DIR" \
  --foreground-only

python3 "$SCRIPT_DIR/analyze-foreground-plates.py" \
  --frames-dir "$HOST_CAPTURE_DIR/frames" \
  --output-json "$ANALYSIS_JSON"

python3 "$SCRIPT_DIR/build-fishing1-foreground-pack.py" \
  --frames-dir "$HOST_CAPTURE_DIR/frames" \
  --output-pack "$PACK_PATH" \
  --output-json "$PACK_JSON"

python3 "$SCRIPT_DIR/build-fishing1-foreground-pack.py" \
  --frames-dir "$HOST_CAPTURE_DIR/frames" \
  --output-pack "$DIRECT_PACK_PATH" \
  --output-json "$DIRECT_PACK_JSON" \
  --delta-from-previous

python3 "$SCRIPT_DIR/build-ps1-rawframe.py" \
  --input "$RAW_FRAME_SOURCE" \
  --output "$RAW_FRAME_PATH"

if [ ! -s "$PACK_PATH" ]; then
  echo "foreground pack was not generated: $PACK_PATH" >&2
  exit 1
fi

if [ ! -s "$RAW_FRAME_PATH" ]; then
  echo "foreground raw frame was not generated: $RAW_FRAME_PATH" >&2
  exit 1
fi

echo "$ANALYSIS_JSON"
echo "$PACK_JSON"
echo "$DIRECT_PACK_JSON"
echo "$PACK_PATH"
echo "$DIRECT_PACK_PATH"
echo "$RAW_FRAME_PATH"
