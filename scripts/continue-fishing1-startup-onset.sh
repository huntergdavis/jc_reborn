#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

OUTPUT_ROOT="${FISHING1_STARTUP_ONSET_OUTPUT:-/tmp/fishing1-startup-earliest-smoke}"
MAX_CHUNKS="${FISHING1_STARTUP_ONSET_MAX_CHUNKS:-1}"
ANNOTATIONS="${FISHING1_FULL_REVIEW_ANNOTATIONS:-$PROJECT_ROOT/vision-artifacts/fishing1-full-annotation-review/annotations.json}"

usage() {
  cat <<'USAGE'
Usage: continue-fishing1-startup-onset.sh [options]

Resume an existing FISHING 1 startup-onset search from the chunk immediately
before the current earliest target chunk.

Options:
  --output DIR       Existing onset-search output root to extend
  --max-chunks N     Number of earlier chunks to scan in this run (default: 1)
  --annotations PATH Full-scene fishing annotations.json
  -h, --help         Show this help
USAGE
}

while [ $# -gt 0 ]; do
  case "$1" in
    --output) OUTPUT_ROOT="$2"; shift 2 ;;
    --max-chunks) MAX_CHUNKS="$2"; shift 2 ;;
    --annotations) ANNOTATIONS="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [ ! -f "$OUTPUT_ROOT/fishing1-startup-onset.json" ]; then
  echo "ERROR: missing onset report: $OUTPUT_ROOT/fishing1-startup-onset.json" >&2
  exit 1
fi

exec bash "$SCRIPT_DIR/find-fishing1-startup-onset.sh" \
  --output "$OUTPUT_ROOT" \
  --annotations "$ANNOTATIONS" \
  --continue-earlier \
  --max-chunks "$MAX_CHUNKS"
