#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

OUTPUT_ROOT="${FISHING1_STARTUP_ONSET_OUTPUT:-/tmp/fishing1-startup-earliest-smoke}"
MAX_CHUNKS="${FISHING1_STARTUP_ONSET_MAX_CHUNKS:-1}"
ANNOTATIONS="${FISHING1_FULL_REVIEW_ANNOTATIONS:-$PROJECT_ROOT/vision-artifacts/fishing1-full-annotation-review/annotations.json}"
BOOT_STRING="${FISHING1_STARTUP_ONSET_BOOT_STRING:-}"
EPOCH_INDEX=""
TARGET_REGIME=""
UNTIL_NON_TARGET=0
UNTIL_SEQ=""
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
  --until-seq N      Keep stepping earlier until the earliest confirmed target
                     sequence is at or below N, or until another stop condition
                     is hit.
  --annotations PATH Full-scene fishing annotations.json
  --boot STRING      Force one BOOTMODE string for every scanned build
  --epoch-index N    Restrict continuation to one binary-library epoch
  --target-regime STR Override startup regime instead of reusing the onset report
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
    --until-seq) UNTIL_SEQ="$2"; shift 2 ;;
    --annotations) ANNOTATIONS="$2"; shift 2 ;;
    --boot) BOOT_STRING="$2"; shift 2 ;;
    --epoch-index) EPOCH_INDEX="$2"; shift 2 ;;
    --target-regime) TARGET_REGIME="$2"; shift 2 ;;
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

if [ -z "$TARGET_REGIME" ]; then
  TARGET_REGIME="$(python3 - "$OUTPUT_ROOT/fishing1-startup-onset.json" <<'PY'
import json
import sys
from pathlib import Path

payload = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
print(payload.get("target_startup_regime") or "")
PY
)"
fi

if [ -z "$EPOCH_INDEX" ]; then
  EPOCH_INDEX="$(python3 - "$OUTPUT_ROOT/fishing1-startup-onset.json" <<'PY'
import json
import sys
from pathlib import Path

payload = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
value = payload.get("epoch_index")
print("" if value is None else value)
PY
)"
fi

write_until_report() {
  python3 - "$OUTPUT_ROOT/fishing1-startup-onset.json" "$CONTINUE_REPORT" "$1" "$MAX_CHUNKS" "$UNTIL_SEQ" "$2" "$3" <<'PY'
import json
import sys
from pathlib import Path

onset = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
first_target = onset.get("first_target") or {}
report = {
    "mode": "until_non_target",
    "requested_max_chunks": int(sys.argv[4]),
    "requested_until_seq": int(sys.argv[5]) if sys.argv[5] else None,
    "wrapper_stopped_reason": sys.argv[3],
    "report_state": "final" if sys.argv[3] != "in_progress" else "in_progress",
    "onset_stopped_reason": onset.get("stopped_reason"),
    "chunks_scanned_this_run": int(sys.argv[6]),
    "earliest_target_sequence": onset.get("earliest_target_sequence"),
    "first_target_state_hash": onset.get("first_target_state_hash") or first_target.get("state_hash"),
    "first_target_result_json": onset.get("first_target_result_json") or first_target.get("result_json"),
    "next_continue_start_seq": onset.get("next_continue_start_seq"),
    "first_non_target_before_earliest_report": onset.get("first_non_target_before_earliest_report"),
    "onset_report": str(Path(sys.argv[1]).resolve()),
}
if sys.argv[7]:
    report["last_completed_chunk_dir"] = sys.argv[7]
Path(sys.argv[2]).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(sys.argv[2])
PY
}

if [ -n "$UNTIL_SEQ" ] && { ! [[ "$UNTIL_SEQ" =~ ^[0-9]+$ ]] || [ "$UNTIL_SEQ" -lt 0 ]; }; then
  echo "ERROR: --until-seq must be a non-negative integer" >&2
  exit 1
fi

if [ "$UNTIL_NON_TARGET" -eq 0 ]; then
  find_args=(
    --output "$OUTPUT_ROOT"
    --annotations "$ANNOTATIONS"
    --continue-earlier
    --max-chunks "$MAX_CHUNKS"
  )
  if [ -n "$BOOT_STRING" ]; then
    find_args+=(--boot "$BOOT_STRING")
  fi
  if [ -n "$EPOCH_INDEX" ]; then
    find_args+=(--epoch-index "$EPOCH_INDEX")
  fi
  if [ -n "$TARGET_REGIME" ]; then
    find_args+=(--target-regime "$TARGET_REGIME")
  fi
  bash "$SCRIPT_DIR/find-fishing1-startup-onset.sh" \
    "${find_args[@]}"
  python3 - "$OUTPUT_ROOT/fishing1-startup-onset.json" "$CONTINUE_REPORT" "single_step" "$MAX_CHUNKS" "$UNTIL_SEQ" <<'PY'
import json
import sys
from pathlib import Path

onset = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
first_target = onset.get("first_target") or {}
report = {
    "mode": sys.argv[3],
    "requested_max_chunks": int(sys.argv[4]),
    "requested_until_seq": int(sys.argv[5]) if sys.argv[5] else None,
    "stopped_reason": onset.get("stopped_reason"),
    "report_state": "final",
    "chunks_scanned_this_run": onset.get("chunks_scanned_this_run"),
    "earliest_target_sequence": onset.get("earliest_target_sequence"),
    "first_target_state_hash": onset.get("first_target_state_hash") or first_target.get("state_hash"),
    "first_target_result_json": onset.get("first_target_result_json") or first_target.get("result_json"),
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

if [ -n "$UNTIL_SEQ" ] && python3 - "$OUTPUT_ROOT/fishing1-startup-onset.json" "$UNTIL_SEQ" <<'PY'
import json
import sys
from pathlib import Path

payload = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
earliest = payload.get("earliest_target_sequence")
floor = int(sys.argv[2])
raise SystemExit(0 if earliest is not None and earliest <= floor else 1)
PY
then
  loop_stop_reason="already_at_or_below_until_seq"
  write_until_report "$loop_stop_reason" 0 ""
  exit 0
fi

chunks_done=0
loop_stop_reason="max_chunks"
last_completed_chunk_dir=""
while [ "$chunks_done" -lt "$MAX_CHUNKS" ]; do
  find_args=(
    --output "$OUTPUT_ROOT"
    --annotations "$ANNOTATIONS"
    --continue-earlier
    --max-chunks 1
  )
  if [ -n "$BOOT_STRING" ]; then
    find_args+=(--boot "$BOOT_STRING")
  fi
  if [ -n "$EPOCH_INDEX" ]; then
    find_args+=(--epoch-index "$EPOCH_INDEX")
  fi
  if [ -n "$TARGET_REGIME" ]; then
    find_args+=(--target-regime "$TARGET_REGIME")
  fi
  bash "$SCRIPT_DIR/find-fishing1-startup-onset.sh" \
    "${find_args[@]}"
  last_completed_chunk_dir="$(python3 - "$OUTPUT_ROOT/fishing1-startup-onset.json" <<'PY'
import json
import sys
from pathlib import Path

payload = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
print(payload.get("earliest_target_chunk_dir") or "")
PY
)"
  chunks_done=$((chunks_done + 1))
  write_until_report "in_progress" "$chunks_done" "$last_completed_chunk_dir" >/dev/null

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

  if [ -n "$UNTIL_SEQ" ] && python3 - "$OUTPUT_ROOT/fishing1-startup-onset.json" "$UNTIL_SEQ" <<'PY'
import json
import sys
from pathlib import Path

payload = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
earliest = payload.get("earliest_target_sequence")
floor = int(sys.argv[2])
raise SystemExit(0 if earliest is not None and earliest <= floor else 1)
PY
  then
    loop_stop_reason="reached_until_seq"
    break
  fi
done

write_until_report "$loop_stop_reason" "$chunks_done" "$last_completed_chunk_dir"
