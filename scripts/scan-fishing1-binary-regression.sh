#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

ANNOTATIONS="${FISHING1_FULL_REVIEW_ANNOTATIONS:-$PROJECT_ROOT/vision-artifacts/fishing1-full-annotation-review/annotations.json}"
OUTPUT_ROOT="${FISHING1_BINLIB_SCAN_OUTPUT:-$PROJECT_ROOT/tmp-regtests/binlib-fishing1-scan}"
FRAMES="${FISHING1_BINLIB_SCAN_FRAMES:-4200}"
START_FRAME="${FISHING1_BINLIB_SCAN_START_FRAME:-0}"
INTERVAL="${FISHING1_BINLIB_SCAN_INTERVAL:-30}"
LIMIT="${FISHING1_BINLIB_SCAN_LIMIT:-}"
START_SEQ=""
END_SEQ=""
DIR_NAMES=()
SKIPPED_NAMES=()

usage() {
  cat <<'USAGE'
Usage: scan-fishing1-binary-regression.sh [options]

Runs FISHING 1 across selected binary-library builds, then summarizes each
result against the full-scene fishing labels.

Options:
  --annotations PATH   Full-scene fishing annotations.json
  --output DIR         Output root for regtest results and summary JSON
  --frames N           Total frames to run per build (default: 4200)
  --start-frame N      First frame to dump (default: 0)
  --interval N         Screenshot interval (default: 30)
  --start-seq N        First binary-library sequence to test
  --end-seq N          Last binary-library sequence to test
  --dir-name NAME      Exact binary-library directory name to test
                       May be repeated.
  --limit N            Limit number of matching builds
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
    --start-seq) START_SEQ="$2"; shift 2 ;;
    --end-seq) END_SEQ="$2"; shift 2 ;;
    --dir-name) DIR_NAMES+=("$2"); shift 2 ;;
    --limit) LIMIT="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [ ! -f "$ANNOTATIONS" ]; then
  echo "ERROR: annotations file not found: $ANNOTATIONS" >&2
  exit 1
fi

mkdir -p "$OUTPUT_ROOT"

run_one() {
  local outdir="$1"
  shift
  "$PROJECT_ROOT/scripts/regtest-binary-library-scene.sh" \
    --scene "FISHING 1" \
    --frames "$FRAMES" \
    --start-frame "$START_FRAME" \
    --interval "$INTERVAL" \
    --output "$outdir" \
    --resume \
    "$@"
}

if [ "${#DIR_NAMES[@]}" -gt 0 ]; then
  for dir_name in "${DIR_NAMES[@]}"; do
    slug="$(printf '%s' "$dir_name" | tr '/ ' '__')"
    if ! run_one "$OUTPUT_ROOT/$slug" --dir-name "$dir_name"; then
      echo "SKIP: no runnable build matched $dir_name" >&2
      SKIPPED_NAMES+=("$dir_name")
    fi
  done
else
  extra=()
  if [ -n "$START_SEQ" ]; then
    extra+=(--start-seq "$START_SEQ")
  fi
  if [ -n "$END_SEQ" ]; then
    extra+=(--end-seq "$END_SEQ")
  fi
  if [ -n "$LIMIT" ]; then
    extra+=(--limit "$LIMIT")
  fi
  run_one "$OUTPUT_ROOT/range" "${extra[@]}"
fi

python3 - "$PROJECT_ROOT" "$OUTPUT_ROOT" "$ANNOTATIONS" "${SKIPPED_NAMES[@]}" <<'PY'
import json
import subprocess
import sys
from pathlib import Path

project_root = Path(sys.argv[1])
output_root = Path(sys.argv[2])
annotations = Path(sys.argv[3])
summarizer = project_root / "scripts" / "summarize-fishing1-result.py"

rows = []
for result_path in sorted(output_root.rglob("result.json")):
    if result_path.parent == output_root:
        continue
    proc = subprocess.run(
        ["python3", str(summarizer), "--annotations", str(annotations), "--result", str(result_path)],
        check=True,
        capture_output=True,
        text=True,
    )
    summary = json.loads(proc.stdout)
    summary["result_json"] = str(result_path)
    rows.append(summary)

report = {
    "annotations": str(annotations),
    "output_root": str(output_root),
    "rows": rows,
    "skipped_dir_names": sys.argv[4:],
}
report_path = output_root / "fishing1-binary-regression-summary.json"
report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(report_path)
PY
