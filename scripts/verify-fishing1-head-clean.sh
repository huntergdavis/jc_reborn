#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

ANNOTATIONS="${FISHING1_FULL_REVIEW_ANNOTATIONS:-$PROJECT_ROOT/vision-artifacts/fishing1-full-annotation-review/annotations.json}"
OUTPUT_ROOT="${FISHING1_HEAD_VERIFY_OUTPUT:-$PROJECT_ROOT/tmp-regtests/fishing1-head-verify}"
FRAMES="${FISHING1_HEAD_VERIFY_FRAMES:-4200}"
START_FRAME="${FISHING1_HEAD_VERIFY_START_FRAME:-0}"
INTERVAL="${FISHING1_HEAD_VERIFY_INTERVAL:-30}"

usage() {
  cat <<'USAGE'
Usage: verify-fishing1-head-clean.sh [options]

Runs FISHING 1 once from the current working tree, builds the exact HEAD
binary-library artifact, reruns that clean build, and compares the results.

Options:
  --annotations PATH   Full-scene fishing annotations.json
  --output DIR         Output root for local/clean results
  --frames N           Total frames to run (default: 4200)
  --start-frame N      First frame to dump (default: 0)
  --interval N         Screenshot interval (default: 30)
  -h, --help           Show this help
USAGE
}

while [ $# -gt 0 ]; do
  case "$1" in
    --annotations) ANNOTATIONS="$2"; shift 2 ;;
    --output) OUTPUT_ROOT="$2"; shift 2 ;;
    --frames) FRAMES="$2"; shift 2 ;;
    --start-frame) START_FRAME="$2"; shift 2 ;;
    --interval) INTERVAL="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [ ! -f "$ANNOTATIONS" ]; then
  echo "ERROR: annotations file not found: $ANNOTATIONS" >&2
  exit 1
fi

mkdir -p "$OUTPUT_ROOT"

LOCAL_DIR="$OUTPUT_ROOT/local"
CLEAN_DIR="$OUTPUT_ROOT/clean"
COMPARE_JSON="$OUTPUT_ROOT/compare.json"
VERIFY_STATUS_JSON="$OUTPUT_ROOT/verify-status.json"

"$PROJECT_ROOT/scripts/regtest-scene.sh" \
  --scene "FISHING 1" \
  --frames "$FRAMES" \
  --start-frame "$START_FRAME" \
  --interval "$INTERVAL" \
  --output "$LOCAL_DIR" \
  --quiet

"$PROJECT_ROOT/scripts/build-binary-library.sh" --start HEAD --end HEAD >/dev/null

HEAD_SHA="$(git rev-parse --short HEAD)"
HEAD_DIR_NAME="$(find "$PROJECT_ROOT/binary-library" -maxdepth 1 -mindepth 1 -type d -name "*_${HEAD_SHA}" -printf '%f\n' | sort | tail -n 1)"

if [ -z "$HEAD_DIR_NAME" ]; then
  echo "ERROR: could not find binary-library entry for HEAD $HEAD_SHA" >&2
  exit 1
fi

"$PROJECT_ROOT/scripts/scan-fishing1-binary-regression.sh" \
  --annotations "$ANNOTATIONS" \
  --output "$CLEAN_DIR" \
  --frames "$FRAMES" \
  --start-frame "$START_FRAME" \
  --interval "$INTERVAL" \
  --dir-name "$HEAD_DIR_NAME" >/dev/null

CLEAN_RESULT="$CLEAN_DIR/$HEAD_DIR_NAME/$HEAD_DIR_NAME/result.json"
if [ ! -f "$CLEAN_RESULT" ]; then
  echo "ERROR: clean result missing: $CLEAN_RESULT" >&2
  exit 1
fi

python3 "$PROJECT_ROOT/scripts/compare-fishing1-runs.py" \
  --annotations "$ANNOTATIONS" \
  --result "$LOCAL_DIR/result.json" \
  --label local_worktree \
  --result "$CLEAN_RESULT" \
  --label clean_head \
  --json-out "$COMPARE_JSON"

python3 - "$LOCAL_DIR/result.json" "$COMPARE_JSON" "$VERIFY_STATUS_JSON" <<'PY'
import json
import sys
from pathlib import Path

local_result_path = Path(sys.argv[1])
compare_path = Path(sys.argv[2])
status_path = Path(sys.argv[3])

local_result = json.loads(local_result_path.read_text(encoding="utf-8"))
compare = json.loads(compare_path.read_text(encoding="utf-8"))

rows = compare.get("rows") or []
if len(rows) != 2:
    status = {
        "ok": False,
        "reason": "unexpected_compare_shape",
        "row_count": len(rows),
    }
    status_path.write_text(json.dumps(status, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(status, indent=2))
    raise SystemExit(5)

local_row, clean_row = rows
local_cov = local_row.get("coverage") or {}
clean_cov = clean_row.get("coverage") or {}

dirty_build_inputs = bool((local_result.get("outcome") or {}).get("dirty_build_inputs"))
metric_keys = [
    "first_visible_frame",
    "last_black_frame",
    "unique_black_hashes",
    "unique_ocean_only_hashes",
    "unique_island_only_hashes",
    "unique_correct_hashes",
    "unique_shoe_hashes",
    "unique_post_correct_pre_shoe_hashes",
]
metric_mismatches = {}
for key in metric_keys:
    local_value = local_cov.get(key)
    clean_value = clean_cov.get(key)
    if local_value != clean_value:
        metric_mismatches[key] = {
            "local": local_value,
            "clean": clean_value,
        }

status = {
    "ok": not dirty_build_inputs and not metric_mismatches,
    "dirty_build_inputs": dirty_build_inputs,
    "metric_mismatches": metric_mismatches,
    "local_state_hash": (local_row.get("result") or {}).get("state_hash"),
    "clean_state_hash": (clean_row.get("result") or {}).get("state_hash"),
}
status_path.write_text(json.dumps(status, indent=2) + "\n", encoding="utf-8")
print(json.dumps(status, indent=2))

if dirty_build_inputs:
    raise SystemExit(3)
if metric_mismatches:
    raise SystemExit(4)
PY

echo "local:  $LOCAL_DIR/result.json"
echo "clean:  $CLEAN_RESULT"
echo "diff:   $COMPARE_JSON"
echo "status: $VERIFY_STATUS_JSON"
