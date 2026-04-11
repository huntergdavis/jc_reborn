#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

OUTPUT_ROOT="${FISHING1_STARTUP_ONSET_OUTPUT:-/tmp/fishing1-startup-earliest-smoke}"
MAX_CHUNKS="${FISHING1_STARTUP_ONSET_MAX_CHUNKS:-1}"
ANNOTATIONS="${FISHING1_FULL_REVIEW_ANNOTATIONS:-$PROJECT_ROOT/vision-artifacts/fishing1-full-annotation-review/annotations.json}"
UNTIL_NON_TARGET=0
CONTINUE_REPORT=""

usage() {
  cat <<'USAGE'
Usage: continue-fishing1-startup-onset.sh [options]

Resume an existing FISHING 1 startup-onset search from the chunk immediately
before the current earliest target chunk.

Options:
  --output DIR       Existing onset-search output root to extend
  --max-chunks N     Number of earlier chunks to scan in this run (default: 1)
  --until-non-target Keep stepping earlier until an earlier non-target chunk
                     is found, or until --max-chunks total chunks are used.
  --annotations PATH Full-scene fishing annotations.json
  --report PATH      Optional output JSON path summarizing this continuation
                     run. Default: <output>/fishing1-startup-continue.json
  -h, --help         Show this help
USAGE
}

while [ $# -gt 0 ]; do
  case "$1" in
    --output) OUTPUT_ROOT="$2"; shift 2 ;;
    --max-chunks) MAX_CHUNKS="$2"; shift 2 ;;
    --until-non-target) UNTIL_NON_TARGET=1; shift ;;
    --annotations) ANNOTATIONS="$2"; shift 2 ;;
    --report) CONTINUE_REPORT="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [ ! -f "$OUTPUT_ROOT/fishing1-startup-onset.json" ]; then
  echo "ERROR: missing onset report: $OUTPUT_ROOT/fishing1-startup-onset.json" >&2
  exit 1
fi

if [ -z "$CONTINUE_REPORT" ]; then
  CONTINUE_REPORT="$OUTPUT_ROOT/fishing1-startup-continue.json"
fi

if [ "$UNTIL_NON_TARGET" -eq 0 ]; then
  bash "$SCRIPT_DIR/find-fishing1-startup-onset.sh" \
    --output "$OUTPUT_ROOT" \
    --annotations "$ANNOTATIONS" \
    --continue-earlier \
    --max-chunks "$MAX_CHUNKS"
  python3 - "$OUTPUT_ROOT/fishing1-startup-onset.json" "$CONTINUE_REPORT" "single_step" "$MAX_CHUNKS" <<'PY'
import json
import sys
from pathlib import Path

onset = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
report = {
    "mode": sys.argv[3],
    "requested_max_chunks": int(sys.argv[4]),
    "stopped_reason": onset.get("stopped_reason"),
    "chunks_scanned_this_run": onset.get("chunks_scanned_this_run"),
    "earliest_target_sequence": onset.get("earliest_target_sequence"),
    "next_continue_start_seq": onset.get("next_continue_start_seq"),
    "first_non_target_before_earliest_report": onset.get("first_non_target_before_earliest_report"),
    "onset_report": str(Path(sys.argv[1]).resolve()),
}
Path(sys.argv[2]).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(sys.argv[2])
PY
  exit 0
fi

if ! [[ "$MAX_CHUNKS" =~ ^[0-9]+$ ]] || [ "$MAX_CHUNKS" -le 0 ]; then
  echo "ERROR: --max-chunks must be a positive integer" >&2
  exit 1
fi

chunks_done=0
loop_stop_reason="max_chunks"
while [ "$chunks_done" -lt "$MAX_CHUNKS" ]; do
  bash "$SCRIPT_DIR/find-fishing1-startup-onset.sh" \
    --output "$OUTPUT_ROOT" \
    --annotations "$ANNOTATIONS" \
    --continue-earlier \
    --max-chunks 1

  if python3 - "$OUTPUT_ROOT/fishing1-startup-onset.json" <<'PY'
import json
import sys
from pathlib import Path

payload = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
raise SystemExit(0 if payload.get("first_non_target_before_earliest_report") else 1)
PY
  then
    loop_stop_reason="found_non_target_before_earliest"
    break
  fi

  chunks_done=$((chunks_done + 1))
done

python3 - "$OUTPUT_ROOT/fishing1-startup-onset.json" "$CONTINUE_REPORT" "$loop_stop_reason" "$MAX_CHUNKS" <<'PY'
import json
import sys
from pathlib import Path

onset = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
report = {
    "mode": "until_non_target",
    "requested_max_chunks": int(sys.argv[4]),
    "wrapper_stopped_reason": sys.argv[3],
    "onset_stopped_reason": onset.get("stopped_reason"),
    "chunks_scanned_this_run": onset.get("chunks_scanned_this_run"),
    "earliest_target_sequence": onset.get("earliest_target_sequence"),
    "next_continue_start_seq": onset.get("next_continue_start_seq"),
    "first_non_target_before_earliest_report": onset.get("first_non_target_before_earliest_report"),
    "onset_report": str(Path(sys.argv[1]).resolve()),
}
Path(sys.argv[2]).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(sys.argv[2])
PY
