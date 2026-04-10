#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

ANNOTATIONS="${FISHING1_FULL_REVIEW_ANNOTATIONS:-$PROJECT_ROOT/vision-artifacts/fishing1-full-annotation-review/annotations.json}"
OUTPUT_ROOT="${FISHING1_STARTUP_ONSET_OUTPUT:-$PROJECT_ROOT/tmp-regtests/fishing1-startup-onset}"
TARGET_STARTUP_REGIME="top_only_then_full_height_startup"
CHUNK_SIZE="${FISHING1_STARTUP_ONSET_CHUNK_SIZE:-10}"
START_SEQ=""
END_SEQ=""

usage() {
  cat <<'USAGE'
Usage: find-fishing1-startup-onset.sh [options]

Walk backward through exact binary-library sequence windows and stop at the
first window that contains the requested FISHING 1 startup regime.

Options:
  --annotations PATH   Full-scene fishing annotations.json
  --output DIR         Output root for chunk scans and onset report
  --target-regime STR  Startup regime to search for
  --chunk-size N       Exact sequence width per scan chunk (default: 10)
  --start-seq N        Highest sequence to begin scanning from
  --end-seq N          Lowest sequence to include before stopping
  -h, --help           Show this help
USAGE
}

while [ $# -gt 0 ]; do
  case "$1" in
    --annotations) ANNOTATIONS="$2"; shift 2 ;;
    --output) OUTPUT_ROOT="$2"; shift 2 ;;
    --target-regime) TARGET_STARTUP_REGIME="$2"; shift 2 ;;
    --chunk-size) CHUNK_SIZE="$2"; shift 2 ;;
    --start-seq) START_SEQ="$2"; shift 2 ;;
    --end-seq) END_SEQ="$2"; shift 2 ;;
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

current_high="$START_SEQ"
chunk_index=0
found_report=""
chunk_history_path="$OUTPUT_ROOT/fishing1-startup-onset-chunks.jsonl"
: > "$chunk_history_path"

while [ "$current_high" -ge "$END_SEQ" ]; do
  current_low=$((current_high - CHUNK_SIZE + 1))
  if [ "$current_low" -lt "$END_SEQ" ]; then
    current_low="$END_SEQ"
  fi

  chunk_dir="$OUTPUT_ROOT/seq_${current_low}_to_${current_high}"
  rm -rf "$chunk_dir"

  bash "$SCRIPT_DIR/scan-fishing1-binary-regression.sh" \
    --output "$chunk_dir" \
    --annotations "$ANNOTATIONS" \
    --exact-start-seq "$current_low" \
    --exact-end-seq "$current_high" \
    --boundary-startup-regime "$TARGET_STARTUP_REGIME" \
    --stop-after-first-startup-regime "$TARGET_STARTUP_REGIME"

  boundary_path="$chunk_dir/fishing1-startup-boundary.json"
  if python3 - "$boundary_path" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
payload = json.loads(path.read_text(encoding="utf-8"))
raise SystemExit(0 if payload.get("first_target") else 1)
PY
  then
    found_report="$boundary_path"
    python3 - "$chunk_history_path" "$current_low" "$current_high" "$chunk_dir" "$boundary_path" "true" <<'PY'
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
with history_path.open("a", encoding="utf-8") as handle:
    handle.write(json.dumps(entry) + "\n")
PY
    break
  fi

  python3 - "$chunk_history_path" "$current_low" "$current_high" "$chunk_dir" "$boundary_path" "false" <<'PY'
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
with history_path.open("a", encoding="utf-8") as handle:
    handle.write(json.dumps(entry) + "\n")
PY

  current_high=$((current_low - 1))
  chunk_index=$((chunk_index + 1))
done

python3 - "$OUTPUT_ROOT" "$TARGET_STARTUP_REGIME" "$START_SEQ" "$END_SEQ" "$found_report" <<'PY'
import json
import sys
from pathlib import Path

output_root = Path(sys.argv[1])
target_regime = sys.argv[2]
start_seq = int(sys.argv[3])
end_seq = int(sys.argv[4])
found_report = sys.argv[5]

report = {
    "output_root": str(output_root),
    "target_startup_regime": target_regime,
    "start_seq": start_seq,
    "end_seq": end_seq,
    "chunk_history_jsonl": str(output_root / "fishing1-startup-onset-chunks.jsonl"),
    "onset_boundary_report": found_report or None,
}

if found_report:
    payload = json.loads(Path(found_report).read_text(encoding="utf-8"))
    report["last_before_target"] = payload.get("last_before_target")
    report["first_target"] = payload.get("first_target")
else:
    report["last_before_target"] = None
    report["first_target"] = None

out_path = output_root / "fishing1-startup-onset.json"
out_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(out_path)
PY
