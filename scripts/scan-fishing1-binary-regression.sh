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
BOUNDARY_STARTUP_REGIME=""

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
  --boundary-startup-regime NAME
                       Also write a startup-regime boundary report using
                       find-fishing1-regression-boundary.py
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
    --boundary-startup-regime) BOUNDARY_STARTUP_REGIME="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [ ! -f "$ANNOTATIONS" ]; then
  echo "ERROR: annotations file not found: $ANNOTATIONS" >&2
  exit 1
fi

mkdir -p "$OUTPUT_ROOT"

refresh_reports() {
  extra=()
  for skipped_name in "${SKIPPED_NAMES[@]}"; do
    extra+=(--skipped-dir-name "$skipped_name")
  done
  if [ -n "$BOUNDARY_STARTUP_REGIME" ]; then
    extra+=(--boundary-startup-regime "$BOUNDARY_STARTUP_REGIME")
  fi
  python3 "$PROJECT_ROOT/scripts/summarize-fishing1-scan-output.py" \
    --output-root "$OUTPUT_ROOT" \
    --annotations "$ANNOTATIONS" \
    "${extra[@]}"
}

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
    refresh_reports >/dev/null
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
  refresh_reports >/dev/null
fi

refresh_reports
