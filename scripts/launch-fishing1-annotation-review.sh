#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTDIR="${1:-$PROJECT_ROOT/vision-artifacts/fishing1-annotation-review}"
PORT="${SCENE_REVIEW_PORT:-8123}"
FILTERED_RESULT_DIR="$OUTDIR/filtered-result"
GENERATED_RUN_DIR="$OUTDIR/regtest-run"
SOURCE_RESULT="${FISHING1_REVIEW_RESULT:-}"
resolve_scene_value() {
  local field="$1"
  local fallback="$2"
  python3 "$PROJECT_ROOT/scripts/get-scene-capture-start.py" \
    --scene "FISHING 1" \
    --field "$field" \
    --default "$fallback"
}
CAPTURE_START_FRAME="${FISHING1_REVIEW_CAPTURE_START_FRAME:-$(resolve_scene_value review_capture_start_frame 3480)}"
CAPTURE_FRAMES="${FISHING1_REVIEW_CAPTURE_FRAMES:-$(resolve_scene_value review_capture_frames 3720)}"
CAPTURE_INTERVAL="${FISHING1_REVIEW_CAPTURE_INTERVAL:-$(resolve_scene_value review_capture_interval 10)}"
START_FRAME="${FISHING1_REVIEW_START_FRAME:-$(resolve_scene_value review_start_frame 3580)}"

if [ -z "$SOURCE_RESULT" ]; then
  "$PROJECT_ROOT/scripts/regtest-scene.sh" \
    --scene "FISHING 1" \
    --frames "$CAPTURE_FRAMES" \
    --start-frame "$CAPTURE_START_FRAME" \
    --interval "$CAPTURE_INTERVAL" \
    --output "$GENERATED_RUN_DIR" \
    --quiet >/dev/null
  SOURCE_RESULT="$GENERATED_RUN_DIR/result.json"
fi

python3 "$PROJECT_ROOT/scripts/filter-result-frames.py" \
  --result "$SOURCE_RESULT" \
  --outdir "$FILTERED_RESULT_DIR" \
  --start-frame "$START_FRAME" >/dev/null

python3 "$PROJECT_ROOT/scripts/generate-scene-annotation-review.py" \
  --scene-id "FISHING-1" \
  --title "FISHING 1 Annotation Review" \
  --all-frames \
  --paired \
  --reference "$PROJECT_ROOT/regtest-references/FISHING-1/result.json" \
  --result "$FILTERED_RESULT_DIR/result.json" \
  --outdir "$OUTDIR" >/dev/null

URL="$(python3 - <<PY
host = "127.0.0.1"
port = int("$PORT")
print(f"http://{host}:{port}/review.html")
PY
)"

echo "$URL"
echo "annotations: $OUTDIR/annotations.json"

if command -v xdg-open >/dev/null 2>&1; then
  ( sleep 1; xdg-open "$URL" >/dev/null 2>&1 || true ) &
fi

exec python3 "$PROJECT_ROOT/scripts/serve-scene-annotation-review.py" \
  --review-dir "$OUTDIR" \
  --host 127.0.0.1 \
  --port "$PORT"
