#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTDIR="${1:-$PROJECT_ROOT/vision-artifacts/fishing1-annotation-review}"
PORT="${SCENE_REVIEW_PORT:-8123}"
FILTERED_RESULT_DIR="$OUTDIR/filtered-result"
GENERATED_RUN_DIR="$OUTDIR/regtest-run"
SOURCE_RESULT="${FISHING1_REVIEW_RESULT:-}"
STABLE_RESULT_DIR="${FISHING1_REVIEW_STABLE_RESULT_DIR:-$PROJECT_ROOT/regtest-results/fishing-1-story17-dense-v3-current}"
STABLE_RESULT="$STABLE_RESULT_DIR/result.json"
FULL_REVIEW_ANNOTATIONS="${FISHING1_FULL_REVIEW_ANNOTATIONS:-$PROJECT_ROOT/vision-artifacts/fishing1-full-annotation-review/annotations.json}"
resolve_scene_value() {
  local field="$1"
  local fallback="$2"
  python3 "$PROJECT_ROOT/scripts/get-scene-capture-start.py" \
    --scene "FISHING 1" \
    --field "$field" \
    --default "$fallback"
}
CAPTURE_START_FRAME="${FISHING1_REVIEW_CAPTURE_START_FRAME:-$(resolve_scene_value review_capture_start_frame 3000)}"
CAPTURE_FRAMES="${FISHING1_REVIEW_CAPTURE_FRAMES:-$(resolve_scene_value review_capture_frames 3360)}"
CAPTURE_INTERVAL="${FISHING1_REVIEW_CAPTURE_INTERVAL:-$(resolve_scene_value review_capture_interval 10)}"
START_FRAME="${FISHING1_REVIEW_START_FRAME:-$(resolve_scene_value review_start_frame 3060)}"
END_FRAME="${FISHING1_REVIEW_END_FRAME:-$(resolve_scene_value review_end_frame 3360)}"

stable_result_matches_profile() {
  python3 - "$STABLE_RESULT" "$CAPTURE_START_FRAME" "$CAPTURE_FRAMES" "$CAPTURE_INTERVAL" <<'PY'
import json
import sys
from pathlib import Path

result_path = Path(sys.argv[1])
expected_start = int(sys.argv[2])
expected_frames = int(sys.argv[3])
expected_interval = int(sys.argv[4])

if not result_path.is_file():
    raise SystemExit(1)

payload = json.loads(result_path.read_text(encoding="utf-8"))
config = payload.get("config") or {}
scene = payload.get("scene") or {}

if int(config.get("start_frame", -1)) != expected_start:
    raise SystemExit(1)
if int(config.get("frames", -1)) != expected_frames:
    raise SystemExit(1)
if int(config.get("interval", -1)) != expected_interval:
    raise SystemExit(1)
if scene.get("boot_string") != "story single 17 seed 1":
    raise SystemExit(1)
PY
  if [ -f "$FULL_REVIEW_ANNOTATIONS" ]; then
    python3 "$PROJECT_ROOT/scripts/validate-fishing1-stable-result.py" \
      --annotations "$FULL_REVIEW_ANNOTATIONS" \
      --result "$STABLE_RESULT" >/dev/null
  fi
}

if [ -z "$SOURCE_RESULT" ]; then
  if stable_result_matches_profile; then
    SOURCE_RESULT="$STABLE_RESULT"
  else
    "$PROJECT_ROOT/scripts/regtest-scene.sh" \
      --scene "FISHING 1" \
      --frames "$CAPTURE_FRAMES" \
      --start-frame "$CAPTURE_START_FRAME" \
      --interval "$CAPTURE_INTERVAL" \
      --output "$GENERATED_RUN_DIR" \
      --quiet >/dev/null
    python3 "$PROJECT_ROOT/scripts/materialize-result-bundle.py" \
      --result "$GENERATED_RUN_DIR/result.json" \
      --outdir "$STABLE_RESULT_DIR" >/dev/null
    SOURCE_RESULT="$STABLE_RESULT"
  fi
fi

python3 "$PROJECT_ROOT/scripts/filter-result-frames.py" \
  --result "$SOURCE_RESULT" \
  --outdir "$FILTERED_RESULT_DIR" \
  --start-frame "$START_FRAME" \
  --end-frame "$END_FRAME" >/dev/null

python3 "$PROJECT_ROOT/scripts/generate-scene-annotation-review.py" \
  --scene-id "FISHING-1" \
  --title "FISHING 1 Annotation Review" \
  --all-query-frames \
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
