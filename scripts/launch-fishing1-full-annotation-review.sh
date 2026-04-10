#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTDIR="${1:-$PROJECT_ROOT/vision-artifacts/fishing1-full-annotation-review}"
PORT="${SCENE_REVIEW_PORT:-8135}"
GENERATED_RUN_DIR="$OUTDIR/regtest-run"
SOURCE_RESULT="${FISHING1_FULL_REVIEW_RESULT:-}"
STABLE_RESULT_DIR="${FISHING1_FULL_REVIEW_STABLE_RESULT_DIR:-$PROJECT_ROOT/regtest-results/fishing-1-story17-full-current}"
STABLE_RESULT="$STABLE_RESULT_DIR/result.json"

resolve_scene_value() {
  local field="$1"
  local fallback="$2"
  python3 "$PROJECT_ROOT/scripts/get-scene-capture-start.py" \
    --scene "FISHING 1" \
    --field "$field" \
    --default "$fallback"
}

START_FRAME="${FISHING1_FULL_REVIEW_START_FRAME:-$(resolve_scene_value full_review_start_frame 0)}"
FRAMES="${FISHING1_FULL_REVIEW_FRAMES:-$(resolve_scene_value full_review_frames 4200)}"
INTERVAL="${FISHING1_FULL_REVIEW_INTERVAL:-$(resolve_scene_value full_review_interval 30)}"

stable_result_matches_profile() {
  python3 - "$STABLE_RESULT" "$START_FRAME" "$FRAMES" "$INTERVAL" <<'PY'
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
if scene.get("boot_string") != "story scene 17 seed 1":
    raise SystemExit(1)
PY
}

if [ -z "$SOURCE_RESULT" ]; then
  if stable_result_matches_profile; then
    SOURCE_RESULT="$STABLE_RESULT"
  else
    "$PROJECT_ROOT/scripts/regtest-scene.sh" \
      --scene "FISHING 1" \
      --frames "$FRAMES" \
      --start-frame "$START_FRAME" \
      --interval "$INTERVAL" \
      --output "$GENERATED_RUN_DIR" \
      --quiet >/dev/null
    python3 "$PROJECT_ROOT/scripts/materialize-result-bundle.py" \
      --result "$GENERATED_RUN_DIR/result.json" \
      --outdir "$STABLE_RESULT_DIR" >/dev/null
    SOURCE_RESULT="$STABLE_RESULT"
  fi
fi

python3 "$PROJECT_ROOT/scripts/generate-scene-annotation-review.py" \
  --scene-id "FISHING-1" \
  --title "FISHING 1 Full PS1 Review" \
  --all-query-frames \
  --reference "$PROJECT_ROOT/regtest-references/FISHING-1/result.json" \
  --result "$SOURCE_RESULT" \
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
