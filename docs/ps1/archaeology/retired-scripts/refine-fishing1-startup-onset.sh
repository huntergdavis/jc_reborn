#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

OUTPUT_ROOT=""
ONSET_JSON=""
ANNOTATIONS="${FISHING1_FULL_REVIEW_ANNOTATIONS:-$PROJECT_ROOT/vision-artifacts/fishing1-full-annotation-review/annotations.json}"
TARGET_REGIME=""
BOOT_STRING=""

usage() {
  cat <<'USAGE'
Usage: refine-fishing1-startup-onset.sh --onset-json PATH --output DIR [options]

Take a coarse fishing startup-onset report and rerun the exact boundary window
one sequence at a time to recover the exact first build in the target startup
regime.

Options:
  --onset-json PATH   Existing fishing1-startup-onset.json
  --output DIR        Output directory for the refined scan
  --annotations PATH  Full-scene fishing annotations.json
  --target-regime STR Override onset target startup regime
  --boot STRING       Force one BOOTMODE string for every scanned build
  -h, --help          Show this help
USAGE
}

while [ $# -gt 0 ]; do
  case "$1" in
    --onset-json) ONSET_JSON="$2"; shift 2 ;;
    --output) OUTPUT_ROOT="$2"; shift 2 ;;
    --annotations) ANNOTATIONS="$2"; shift 2 ;;
    --target-regime) TARGET_REGIME="$2"; shift 2 ;;
    --boot) BOOT_STRING="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [ -z "$ONSET_JSON" ] || [ -z "$OUTPUT_ROOT" ]; then
  usage >&2
  exit 1
fi

if [ ! -f "$ONSET_JSON" ]; then
  echo "ERROR: onset report not found: $ONSET_JSON" >&2
  exit 1
fi
if [ ! -f "$ANNOTATIONS" ]; then
  echo "ERROR: annotations file not found: $ANNOTATIONS" >&2
  exit 1
fi

read -r start_seq end_seq target_regime_from_report boot_from_report < <(
  python3 - "$ONSET_JSON" <<'PY'
import json
import re
import sys
from pathlib import Path

payload = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
earliest_dir = payload.get("earliest_target_chunk_dir") or ""
non_target_dir = payload.get("first_non_target_before_earliest_chunk_dir") or ""
target_regime = payload.get("target_startup_regime") or ""
boot_string = ""
first_target = payload.get("first_target") or {}
target_result = first_target.get("result_json")
if target_result:
    result_payload = json.loads(Path(target_result).read_text(encoding="utf-8"))
    boot_string = (((result_payload.get("scene") or {}).get("boot_string")) or "")

def parse_chunk_dir(value: str):
    match = re.search(r"seq_(\d+)_to_(\d+)", value or "")
    if not match:
        return None
    return int(match.group(1)), int(match.group(2))

earliest_range = parse_chunk_dir(earliest_dir)
non_target_range = parse_chunk_dir(non_target_dir)
if earliest_range is None:
    raise SystemExit("missing earliest_target_chunk_dir in onset report")

start_seq = earliest_range[1]
end_seq = earliest_range[0]
if non_target_range is not None:
    end_seq = non_target_range[1]

print(start_seq, end_seq, target_regime, boot_string)
PY
)

if [ -z "$TARGET_REGIME" ]; then
  TARGET_REGIME="$target_regime_from_report"
fi
if [ -z "$BOOT_STRING" ]; then
  BOOT_STRING="$boot_from_report"
fi

mkdir -p "$OUTPUT_ROOT"

args=(
  --output "$OUTPUT_ROOT"
  --annotations "$ANNOTATIONS"
  --chunk-size 1
  --start-seq "$start_seq"
  --end-seq "$end_seq"
  --continue-through-targets
  --target-regime "$TARGET_REGIME"
)

if [ -n "$BOOT_STRING" ]; then
  args+=(--boot "$BOOT_STRING")
fi

bash "$SCRIPT_DIR/find-fishing1-startup-onset.sh" "${args[@]}"
