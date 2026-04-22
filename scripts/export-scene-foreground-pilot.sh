#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_DIR="${1:-}"
SCENE_SLUG="${2:-fishing1}"
SCENE_NAME="${3:-FISHING 1}"
PACK_BASENAME="${4:-$(printf '%s' "$SCENE_SLUG" | tr '[:lower:]' '[:upper:]' | tr -cd 'A-Z0-9')}"
RAW_FRAME_INDEX="${5:-24}"
RAW_BASENAME="${6:-FISH}"

if [ -z "$OUTPUT_DIR" ]; then
  OUTPUT_DIR="$PROJECT_ROOT/host-results/${SCENE_SLUG}-foreground-pilot"
fi

HOST_CAPTURE_DIR="$OUTPUT_DIR/host-capture"
HOST_CAPTURE_FULL_DIR="$OUTPUT_DIR/host-capture-full"
ANALYSIS_JSON="$OUTPUT_DIR/foreground-analysis.json"
PACK_PATH="$PROJECT_ROOT/generated/ps1/foreground/${PACK_BASENAME}.FG1"
PACK_JSON="$OUTPUT_DIR/foreground-pack.json"
DIRECT_PACK_PATH="$PROJECT_ROOT/generated/ps1/foreground/${PACK_BASENAME}D.FG1"
DIRECT_PACK_JSON="$OUTPUT_DIR/foreground-pack-direct.json"
RAW_FRAME_NAME="$(printf 'frame_%05d.bmp' "$RAW_FRAME_INDEX")"
RAW_FRAME_SOURCE="$HOST_CAPTURE_DIR/frames/$RAW_FRAME_NAME"
RAW_FRAME_PATH="$PROJECT_ROOT/generated/ps1/foreground/${RAW_BASENAME}${RAW_FRAME_INDEX}.RAW"
mkdir -p "$OUTPUT_DIR"

"$SCRIPT_DIR/capture-host-scene.sh" \
  --scene "$SCENE_NAME" \
  --mode story-single \
  --seed 1 \
  --start-frame 0 \
  --interval 1 \
  --until-exit \
  --no-stamp \
  --output "$HOST_CAPTURE_DIR" \
  --foreground-only

# Second capture without --foreground-only. Same seed/range so frame indices
# line up. We use this to (a) pick a pristine scene-base frame (frame 0 has no
# Johnny / pole / line yet) and (b) recover pole + line pixels for the cast-arc
# frames where the foreground-only ledger replay drops them (the DRAW_LINE
# opcode writes directly to the ttm layer and is not replayed by
# grCaptureBlitForegroundLedger).
"$SCRIPT_DIR/capture-host-scene.sh" \
  --scene "$SCENE_NAME" \
  --mode story-single \
  --seed 1 \
  --start-frame 0 \
  --interval 1 \
  --until-exit \
  --no-stamp \
  --output "$HOST_CAPTURE_FULL_DIR"

# Scene-base augmentation is no longer needed: grDrawLine / grDrawPixel
# now record to the capture ledger and replay post-mask, so DRAW_LINE
# strokes flow into fg-only naturally. Augmentation was adding wave-crest
# speckle at the waterline (frame N foam vs frame 0 foam) which persisted
# in the pack as a starfish-looking splotch after the throw animation.

python3 "$SCRIPT_DIR/analyze-foreground-plates.py" \
  --frames-dir "$HOST_CAPTURE_DIR/frames" \
  --output-json "$ANALYSIS_JSON"

python3 "$SCRIPT_DIR/build-scene-foreground-pack.py" \
  --scene-label "$SCENE_NAME" \
  --frames-dir "$HOST_CAPTURE_DIR/frames" \
  --frame-meta-dir "$HOST_CAPTURE_DIR/frame-meta" \
  --sound-events "$HOST_CAPTURE_DIR/sound-events.jsonl" \
  --output-pack "$PACK_PATH" \
  --output-json "$PACK_JSON"

python3 "$SCRIPT_DIR/build-scene-foreground-pack.py" \
  --scene-label "$SCENE_NAME" \
  --frames-dir "$HOST_CAPTURE_DIR/frames" \
  --sound-events "$HOST_CAPTURE_DIR/sound-events.jsonl" \
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
