#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

ANNOTATIONS="${FISHING1_FULL_REVIEW_ANNOTATIONS:-$PROJECT_ROOT/vision-artifacts/fishing1-full-annotation-review/annotations.json}"
OUTPUT_ROOT="${FISHING1_VISIBILITY_ONSET_OUTPUT:-$PROJECT_ROOT/tmp-regtests/fishing1-visibility-onset}"
CHUNK_SIZE="${FISHING1_VISIBILITY_ONSET_CHUNK_SIZE:-10}"
MIN_FIRST_VISIBLE="${FISHING1_VISIBILITY_ONSET_MIN_FIRST_VISIBLE:-1200}"
MIN_FIRST_FULL_HEIGHT="${FISHING1_VISIBILITY_ONSET_MIN_FIRST_FULL_HEIGHT:-1200}"
BOOT_STRING="${FISHING1_VISIBILITY_ONSET_BOOT_STRING:-}"
START_SEQ=""
END_SEQ=""
START_SEQ_EXPLICIT=0
END_SEQ_EXPLICIT=0
CONTINUE_THROUGH_TARGETS=0
RESUME=0
MAX_CHUNKS=""
CONTINUE_EARLIER=0
REBUILD_FROM_HISTORY=0

usage() {
  cat <<'USAGE'
Usage: find-fishing1-visibility-onset.sh [options]

Walk backward through exact binary-library sequence windows and stop at the
first window that reaches the requested FISHING 1 startup visibility thresholds.

Options:
  --annotations PATH       Full-scene fishing annotations.json
  --output DIR             Output root for chunk scans and onset report
  --min-first-visible N    Search target for first_visible >= N (default: 1200)
  --min-first-full-height N
                           Search target for first_full_height_visible >= N
                           (default: 1200)
  --boot STRING            Force one BOOTMODE string for every scanned build
  --chunk-size N           Exact sequence width per scan chunk (default: 10)
  --start-seq N            Highest sequence to begin scanning from
  --end-seq N              Lowest sequence to include before stopping
  --continue-through-targets
                           Keep walking backward through target chunks until
                           the first non-target chunk after them, then report
                           the earliest target chunk seen.
  --resume                 Reuse existing chunk boundary reports under --output
                           instead of rerunning those chunk scans.
  --max-chunks N           Stop after scanning at most N chunks in this run.
  --continue-earlier       Reuse the current earliest target chunk under
                           --output and continue from the chunk immediately
                           before it.
  --rebuild-from-history   Rebuild fishing1-visibility-onset.json from the
                           existing chunk history and boundary reports without
                           running scans.
  -h, --help               Show this help
USAGE
}

while [ $# -gt 0 ]; do
  case "$1" in
    --annotations) ANNOTATIONS="$2"; shift 2 ;;
    --output) OUTPUT_ROOT="$2"; shift 2 ;;
    --min-first-visible) MIN_FIRST_VISIBLE="$2"; shift 2 ;;
    --min-first-full-height) MIN_FIRST_FULL_HEIGHT="$2"; shift 2 ;;
    --boot) BOOT_STRING="$2"; shift 2 ;;
    --chunk-size) CHUNK_SIZE="$2"; shift 2 ;;
    --start-seq) START_SEQ="$2"; START_SEQ_EXPLICIT=1; shift 2 ;;
    --end-seq) END_SEQ="$2"; END_SEQ_EXPLICIT=1; shift 2 ;;
    --continue-through-targets) CONTINUE_THROUGH_TARGETS=1; shift ;;
    --resume) RESUME=1; shift ;;
    --max-chunks) MAX_CHUNKS="$2"; shift 2 ;;
    --continue-earlier) CONTINUE_EARLIER=1; shift ;;
    --rebuild-from-history) REBUILD_FROM_HISTORY=1; RESUME=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [ ! -f "$ANNOTATIONS" ]; then
  echo "ERROR: annotations file not found: $ANNOTATIONS" >&2
  exit 1
fi

if ! [[ "$CHUNK_SIZE" =~ ^[0-9]+$ ]] || [ "$CHUNK_SIZE" -le 0 ]; then
  echo "ERROR: --chunk-size must be a positive integer" >&2
  exit 1
fi
if ! [[ "$MIN_FIRST_VISIBLE" =~ ^[0-9]+$ ]] || [ "$MIN_FIRST_VISIBLE" -lt 0 ]; then
  echo "ERROR: --min-first-visible must be a non-negative integer" >&2
  exit 1
fi
if ! [[ "$MIN_FIRST_FULL_HEIGHT" =~ ^[0-9]+$ ]] || [ "$MIN_FIRST_FULL_HEIGHT" -lt 0 ]; then
  echo "ERROR: --min-first-full-height must be a non-negative integer" >&2
  exit 1
fi
if [ -n "$MAX_CHUNKS" ] && { ! [[ "$MAX_CHUNKS" =~ ^[0-9]+$ ]] || [ "$MAX_CHUNKS" -le 0 ]; }; then
  echo "ERROR: --max-chunks must be a positive integer" >&2
  exit 1
fi

readarray -t SUCCESS_SEQS < <(python3 - <<'PY'
import json
from pathlib import Path

entries = json.loads(Path("binary-library/index.json").read_text(encoding="utf-8"))
seqs = sorted({entry.get("sequence") for entry in entries if (entry.get("build") or {}).get("status") == "success"})
for seq in seqs:
    if isinstance(seq, int):
        print(seq)
PY
)

if [ "${#SUCCESS_SEQS[@]}" -eq 0 ]; then
  echo "ERROR: no successful binary-library sequences found" >&2
  exit 1
fi

if [ -z "$START_SEQ" ]; then
  START_SEQ="${SUCCESS_SEQS[-1]}"
fi
if [ -z "$END_SEQ" ]; then
  END_SEQ="${SUCCESS_SEQS[0]}"
fi

mkdir -p "$OUTPUT_ROOT"
onset_path="$OUTPUT_ROOT/fishing1-visibility-onset.json"
chunk_history_path="$OUTPUT_ROOT/fishing1-visibility-onset-chunks.jsonl"

if [ "$CONTINUE_EARLIER" -eq 1 ]; then
  RESUME=1
  CONTINUE_THROUGH_TARGETS=1
  if [ ! -f "$onset_path" ]; then
    echo "ERROR: --continue-earlier requires existing $onset_path" >&2
    exit 1
  fi
  read -r continued_start continued_end < <(python3 - "$onset_path" <<'PY'
import json
import re
import sys
from pathlib import Path

payload = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
chunk_dir = payload.get("earliest_target_chunk_dir") or ""
match = re.search(r"seq_(\d+)_to_(\d+)", chunk_dir)
if not match:
    raise SystemExit(1)
chunk_start = int(match.group(1))
end_seq = int(payload.get("end_seq"))
print(chunk_start - 1, end_seq)
PY
  )
  START_SEQ="$continued_start"
  if [ "$END_SEQ_EXPLICIT" -eq 0 ]; then
    END_SEQ="${SUCCESS_SEQS[0]}"
  fi
fi

if [ "$RESUME" -eq 1 ] && [ ! -f "$chunk_history_path" ]; then
  : > "$chunk_history_path"
fi
if [ "$RESUME" -eq 0 ]; then
  : > "$chunk_history_path"
fi

append_chunk_history() {
  python3 - "$chunk_history_path" "$1" "$2" "$3" "$4" "$5" <<'PY'
import json
import sys
from pathlib import Path

history_path = Path(sys.argv[1])
entry = {
    "chunk_start_seq": int(sys.argv[2]),
    "chunk_end_seq": int(sys.argv[3]),
    "chunk_dir": sys.argv[4],
    "boundary_report": sys.argv[5],
    "found_target": sys.argv[6] == "true",
}

existing = set()
if history_path.exists():
    for line in history_path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        existing.add((row.get("chunk_start_seq"), row.get("chunk_end_seq")))

key = (entry["chunk_start_seq"], entry["chunk_end_seq"])
if key not in existing:
    with history_path.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(entry) + "\n")
PY
}

write_report() {
  python3 - "$OUTPUT_ROOT" "$MIN_FIRST_VISIBLE" "$MIN_FIRST_FULL_HEIGHT" "$START_SEQ" "$END_SEQ" "$1" "$CONTINUE_THROUGH_TARGETS" "$RESUME" "$MAX_CHUNKS" "$CONTINUE_EARLIER" "$2" "$3" <<'PY'
import json
import re
import sys
from pathlib import Path

output_root = Path(sys.argv[1])
min_first_visible = int(sys.argv[2])
min_first_full_height = int(sys.argv[3])
start_seq = int(sys.argv[4])
end_seq = int(sys.argv[5])
stopped_reason = sys.argv[6]
continue_through_targets = sys.argv[7] == "1"
resume = sys.argv[8] == "1"
max_chunks = int(sys.argv[9]) if sys.argv[9] else None
continue_earlier = sys.argv[10] == "1"
chunks_scanned = int(sys.argv[11])
last_boundary_report = sys.argv[12] or None

def extract_sequence(value):
    if not value:
        return None
    match = re.search(r"/(\d+)_", value)
    if match:
        return int(match.group(1))
    match = re.search(r"seq_(\d+)_to_(\d+)", value)
    if match:
        return int(match.group(1))
    return None

history_path = output_root / "fishing1-visibility-onset-chunks.jsonl"
history_rows = []
if history_path.exists():
    for line in history_path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        if isinstance(row.get("chunk_start_seq"), int):
            history_rows.append(row)
history_rows.sort(key=lambda row: row["chunk_start_seq"])

history_target = next((row for row in history_rows if row.get("found_target")), None)
history_non_target = None
if history_target is not None:
    for row in history_rows:
        if row["chunk_start_seq"] < history_target["chunk_start_seq"] and not row.get("found_target"):
            history_non_target = row

report = {
    "output_root": str(output_root),
    "target_visibility_thresholds": {
        "min_first_visible": min_first_visible,
        "min_first_full_height": min_first_full_height,
    },
    "start_seq": start_seq,
    "end_seq": end_seq,
    "continue_through_targets": continue_through_targets,
    "resume": resume,
    "max_chunks": max_chunks,
    "continue_earlier": continue_earlier,
    "chunks_scanned_this_run": chunks_scanned,
    "stopped_reason": stopped_reason,
    "chunk_history_jsonl": str(history_path),
    "onset_boundary_report": last_boundary_report,
    "earliest_target_boundary_report": None,
    "earliest_target_chunk_dir": None,
    "earliest_target_sequence": None,
    "next_continue_start_seq": None,
    "first_non_target_before_earliest_report": None,
    "first_non_target_before_earliest_chunk_dir": None,
    "last_before_target": None,
    "first_target": None,
    "first_target_state_hash": None,
    "first_target_result_json": None,
}

if history_target is not None:
    report["earliest_target_boundary_report"] = history_target.get("boundary_report")
    report["earliest_target_chunk_dir"] = history_target.get("chunk_dir")
    report["earliest_target_sequence"] = history_target.get("chunk_start_seq")
if history_non_target is not None:
    report["first_non_target_before_earliest_report"] = history_non_target.get("boundary_report")
    report["first_non_target_before_earliest_chunk_dir"] = history_non_target.get("chunk_dir")

if report["earliest_target_sequence"] is not None and report["earliest_target_sequence"] > end_seq:
    report["next_continue_start_seq"] = report["earliest_target_sequence"] - 1
if report["first_non_target_before_earliest_report"]:
    report["next_continue_start_seq"] = None

if report["earliest_target_boundary_report"]:
    payload = json.loads(Path(report["earliest_target_boundary_report"]).read_text(encoding="utf-8"))
    report["last_before_target"] = payload.get("last_before_target")
    report["first_target"] = payload.get("first_target")
    report["first_target_state_hash"] = (payload.get("first_target") or {}).get("state_hash")
    report["first_target_result_json"] = (payload.get("first_target") or {}).get("result_json")
    target_seq = extract_sequence(report["first_target_result_json"])
    if target_seq is not None:
        report["earliest_target_sequence"] = target_seq

out_path = output_root / "fishing1-visibility-onset.json"
out_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(out_path)
PY
}

if [ "$REBUILD_FROM_HISTORY" -eq 1 ]; then
  write_report "rebuilt_from_history" 0 ""
  exit 0
fi

current_high="$START_SEQ"
chunk_index=0
last_boundary_report=""
stopped_reason=""

while [ "$current_high" -ge "$END_SEQ" ]; do
  if [ -n "$MAX_CHUNKS" ] && [ "$chunk_index" -ge "$MAX_CHUNKS" ]; then
    stopped_reason="max_chunks"
    break
  fi

  current_low=$((current_high - CHUNK_SIZE + 1))
  if [ "$current_low" -lt "$END_SEQ" ]; then
    current_low="$END_SEQ"
  fi

  chunk_dir="$OUTPUT_ROOT/seq_${current_low}_to_${current_high}"
  boundary_path="$chunk_dir/fishing1-visibility-boundary.json"
  if [ "$RESUME" -eq 1 ] && [ -f "$boundary_path" ]; then
    :
  else
    rm -rf "$chunk_dir"
    scan_args=()
    if [ -n "$BOOT_STRING" ]; then
      scan_args+=(--boot "$BOOT_STRING")
    fi
    bash "$SCRIPT_DIR/scan-fishing1-binary-regression.sh" \
      --output "$chunk_dir" \
      --annotations "$ANNOTATIONS" \
      --exact-start-seq "$current_low" \
      --exact-end-seq "$current_high" \
      --reverse-exact \
      --boundary-min-first-visible "$MIN_FIRST_VISIBLE" \
      --boundary-min-first-full-height "$MIN_FIRST_FULL_HEIGHT" \
      --stop-after-min-first-visible "$MIN_FIRST_VISIBLE" \
      --stop-after-min-first-full-height "$MIN_FIRST_FULL_HEIGHT" \
      "${scan_args[@]}"
  fi
  last_boundary_report="$boundary_path"

  if python3 - "$boundary_path" <<'PY'
import json
import sys
from pathlib import Path

payload = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
raise SystemExit(0 if payload.get("first_target") else 1)
PY
  then
    append_chunk_history "$current_low" "$current_high" "$chunk_dir" "$boundary_path" "true"
    chunk_index=$((chunk_index + 1))
    if [ "$CONTINUE_THROUGH_TARGETS" -eq 0 ]; then
      stopped_reason="found_target"
      break
    fi
    current_high=$((current_low - 1))
    continue
  fi

  append_chunk_history "$current_low" "$current_high" "$chunk_dir" "$boundary_path" "false"
  chunk_index=$((chunk_index + 1))

  if python3 - "$chunk_history_path" <<'PY'
import json
import sys
from pathlib import Path

rows = []
for line in Path(sys.argv[1]).read_text(encoding="utf-8").splitlines():
    if not line.strip():
        continue
    row = json.loads(line)
    if isinstance(row.get("chunk_start_seq"), int):
        rows.append(row)
rows.sort(key=lambda row: row["chunk_start_seq"])
target = next((row for row in rows if row.get("found_target")), None)
raise SystemExit(0 if target is not None else 1)
PY
  then
    stopped_reason="found_non_target_before_earliest"
    break
  fi

  current_high=$((current_low - 1))
done

if [ -z "$stopped_reason" ]; then
  if python3 - "$chunk_history_path" <<'PY'
import json
import sys
from pathlib import Path

rows = []
for line in Path(sys.argv[1]).read_text(encoding="utf-8").splitlines():
    if not line.strip():
        continue
    row = json.loads(line)
    if isinstance(row.get("chunk_start_seq"), int):
        rows.append(row)
target = next((row for row in rows if row.get("found_target")), None)
raise SystemExit(0 if target is not None else 1)
PY
  then
    stopped_reason="reached_end_with_targets"
  else
    stopped_reason="reached_end_without_target"
  fi
fi

write_report "$stopped_reason" "$chunk_index" "$last_boundary_report"
