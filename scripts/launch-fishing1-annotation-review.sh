#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTDIR="${1:-$PROJECT_ROOT/vision-artifacts/fishing1-annotation-review}"
PORT="${SCENE_REVIEW_PORT:-8123}"
FILTERED_RESULT_DIR="$OUTDIR/filtered-result"

python3 "$PROJECT_ROOT/scripts/filter-result-frames.py" \
  --result "$PROJECT_ROOT/tmp-regtests/fishing1-zonecommit/result.json" \
  --outdir "$FILTERED_RESULT_DIR" >/dev/null

python3 "$PROJECT_ROOT/scripts/generate-scene-annotation-review.py" \
  --scene-id "FISHING-1" \
  --title "FISHING 1 Annotation Review" \
  --all-frames \
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
