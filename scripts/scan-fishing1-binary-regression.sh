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
BOOT_STRING="${FISHING1_BINLIB_SCAN_BOOT_STRING:-}"
LIMIT="${FISHING1_BINLIB_SCAN_LIMIT:-}"
START_SEQ=""
END_SEQ=""
EXACT_START_SEQ=""
EXACT_END_SEQ=""
REVERSE_EXACT=0
DIR_NAMES=()
SKIPPED_NAMES=()
BOUNDARY_STARTUP_REGIME=""
BOUNDARY_MIN_FIRST_VISIBLE=""
BOUNDARY_MIN_FIRST_FULL_HEIGHT=""
STOP_AFTER_FIRST_STARTUP_REGIME=""
STOP_AFTER_MIN_FIRST_VISIBLE=""
STOP_AFTER_MIN_FIRST_FULL_HEIGHT=""

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
  --boot STRING        Force one BOOTMODE string for every scanned build
  --start-seq N        First binary-library sequence to test
  --end-seq N          Last binary-library sequence to test
  --exact-start-seq N  Expand exact runnable dir names from index.json starting
                       at sequence N. Supports early-stop startup searches.
  --exact-end-seq N    Expand exact runnable dir names from index.json ending
                       at sequence N. Supports early-stop startup searches.
  --reverse-exact      Reverse the exact expanded dir-name order so higher
                       sequence numbers run first inside the exact range.
  --dir-name NAME      Exact binary-library directory name to test
                       May be repeated.
  --limit N            Limit number of matching builds
  --boundary-startup-regime NAME
                       Also write a startup-regime boundary report using
                       find-fishing1-regression-boundary.py
  --boundary-min-first-visible N
                       Also write a first-visible threshold boundary report
  --boundary-min-first-full-height N
                       Also write a first-full-height threshold boundary report
  --stop-after-first-startup-regime NAME
                       Stop the exact dir-name scan once the summary first
                       reaches the requested startup regime.
  --stop-after-min-first-visible N
                       Stop the exact dir-name scan once a row first reaches
                       first_visible >= N.
  --stop-after-min-first-full-height N
                       Stop the exact dir-name scan once a row first reaches
                       first_full_height_visible >= N.
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
    --boot) BOOT_STRING="$2"; shift 2 ;;
    --start-seq) START_SEQ="$2"; shift 2 ;;
    --end-seq) END_SEQ="$2"; shift 2 ;;
    --exact-start-seq) EXACT_START_SEQ="$2"; shift 2 ;;
    --exact-end-seq) EXACT_END_SEQ="$2"; shift 2 ;;
    --reverse-exact) REVERSE_EXACT=1; shift ;;
    --dir-name) DIR_NAMES+=("$2"); shift 2 ;;
    --limit) LIMIT="$2"; shift 2 ;;
    --boundary-startup-regime) BOUNDARY_STARTUP_REGIME="$2"; shift 2 ;;
    --boundary-min-first-visible) BOUNDARY_MIN_FIRST_VISIBLE="$2"; shift 2 ;;
    --boundary-min-first-full-height) BOUNDARY_MIN_FIRST_FULL_HEIGHT="$2"; shift 2 ;;
    --stop-after-first-startup-regime) STOP_AFTER_FIRST_STARTUP_REGIME="$2"; shift 2 ;;
    --stop-after-min-first-visible) STOP_AFTER_MIN_FIRST_VISIBLE="$2"; shift 2 ;;
    --stop-after-min-first-full-height) STOP_AFTER_MIN_FIRST_FULL_HEIGHT="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [ ! -f "$ANNOTATIONS" ]; then
  echo "ERROR: annotations file not found: $ANNOTATIONS" >&2
  exit 1
fi

if { [ -n "$EXACT_START_SEQ" ] && [ -z "$EXACT_END_SEQ" ]; } || { [ -z "$EXACT_START_SEQ" ] && [ -n "$EXACT_END_SEQ" ]; }; then
  echo "ERROR: --exact-start-seq and --exact-end-seq must be provided together" >&2
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
  if [ -n "$BOUNDARY_MIN_FIRST_VISIBLE" ]; then
    extra+=(--boundary-min-first-visible "$BOUNDARY_MIN_FIRST_VISIBLE")
  fi
  if [ -n "$BOUNDARY_MIN_FIRST_FULL_HEIGHT" ]; then
    extra+=(--boundary-min-first-full-height "$BOUNDARY_MIN_FIRST_FULL_HEIGHT")
  fi
  python3 "$PROJECT_ROOT/scripts/summarize-fishing1-scan-output.py" \
    --output-root "$OUTPUT_ROOT" \
    --annotations "$ANNOTATIONS" \
    "${extra[@]}"
}

run_one() {
  local outdir="$1"
  shift
  local args=()
  if [ -n "$BOOT_STRING" ]; then
    args+=(--boot "$BOOT_STRING")
  fi
  "$PROJECT_ROOT/scripts/regtest-binary-library-scene.sh" \
    --scene "FISHING 1" \
    --frames "$FRAMES" \
    --start-frame "$START_FRAME" \
    --interval "$INTERVAL" \
    --output "$outdir" \
    --resume \
    "${args[@]}" \
    "$@"
}

startup_regime_seen() {
  local target="$1"
  python3 - "$OUTPUT_ROOT/fishing1-binary-regression-summary.json" "$target" <<'PY'
import json
import sys
from pathlib import Path

summary_path = Path(sys.argv[1])
target = sys.argv[2]
if not summary_path.exists():
    raise SystemExit(1)
payload = json.loads(summary_path.read_text(encoding="utf-8"))
for row in payload.get("rows") or []:
    if row.get("startup_regime") == target:
        raise SystemExit(0)
raise SystemExit(1)
PY
}

visibility_threshold_seen() {
  python3 - "$OUTPUT_ROOT/fishing1-binary-regression-summary.json" "$1" "$2" <<'PY'
import json
import sys
from pathlib import Path

summary_path = Path(sys.argv[1])
min_first_visible = int(sys.argv[2]) if sys.argv[2] else None
min_first_full_height = int(sys.argv[3]) if sys.argv[3] else None
if not summary_path.exists():
    raise SystemExit(1)
payload = json.loads(summary_path.read_text(encoding="utf-8"))
for row in payload.get("rows") or []:
    cov = row.get("coverage") or {}
    first_visible = cov.get("first_visible_frame")
    first_full_height = cov.get("first_full_height_visible_frame")
    if min_first_visible is not None:
        if first_visible is None or first_visible < min_first_visible:
            continue
    if min_first_full_height is not None:
        if first_full_height is None or first_full_height < min_first_full_height:
            continue
    raise SystemExit(0)
raise SystemExit(1)
PY
}

expand_exact_dir_names() {
  python3 - "$PROJECT_ROOT/binary-library/index.json" "$EXACT_START_SEQ" "$EXACT_END_SEQ" "$REVERSE_EXACT" <<'PY'
import json
import sys
from pathlib import Path

index_path = Path(sys.argv[1])
start_seq = int(sys.argv[2])
end_seq = int(sys.argv[3])
reverse = sys.argv[4] == "1"
entries = json.loads(index_path.read_text(encoding="utf-8"))

rows = []
for entry in entries:
    seq = entry.get("sequence")
    if not isinstance(seq, int) or seq < start_seq or seq > end_seq:
        continue
    build = entry.get("build") or {}
    if build.get("status") != "success":
        continue
    dir_name = entry.get("dir_name")
    if not dir_name:
        continue
    commit = entry.get("commit") or {}
    rows.append((seq, commit.get("date") or "", dir_name))

rows.sort(reverse=reverse)
for _, _, dir_name in rows:
    print(dir_name)
PY
}

if [ -n "$EXACT_START_SEQ" ]; then
  while IFS= read -r dir_name; do
    [ -n "$dir_name" ] || continue
    DIR_NAMES+=("$dir_name")
  done < <(expand_exact_dir_names)
fi

if [ -n "$EXACT_START_SEQ" ] && [ "${#DIR_NAMES[@]}" -eq 0 ]; then
  echo "INFO: no successful exact builds matched ${EXACT_START_SEQ}..${EXACT_END_SEQ}" >&2
  refresh_reports
  exit 0
fi

if [ "${#DIR_NAMES[@]}" -gt 0 ]; then
  for dir_name in "${DIR_NAMES[@]}"; do
    slug="$(printf '%s' "$dir_name" | tr '/ ' '__')"
    if ! run_one "$OUTPUT_ROOT/$slug" --dir-name "$dir_name"; then
      echo "SKIP: no runnable build matched $dir_name" >&2
      SKIPPED_NAMES+=("$dir_name")
    fi
    refresh_reports >/dev/null
    if [ -n "$STOP_AFTER_FIRST_STARTUP_REGIME" ] && startup_regime_seen "$STOP_AFTER_FIRST_STARTUP_REGIME"; then
      echo "STOP: found startup regime $STOP_AFTER_FIRST_STARTUP_REGIME" >&2
      break
    fi
    if { [ -n "$STOP_AFTER_MIN_FIRST_VISIBLE" ] || [ -n "$STOP_AFTER_MIN_FIRST_FULL_HEIGHT" ]; } &&
       visibility_threshold_seen "$STOP_AFTER_MIN_FIRST_VISIBLE" "$STOP_AFTER_MIN_FIRST_FULL_HEIGHT"; then
      echo "STOP: reached visibility threshold first_visible>=$STOP_AFTER_MIN_FIRST_VISIBLE first_full_height>=$STOP_AFTER_MIN_FIRST_FULL_HEIGHT" >&2
      break
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
  refresh_reports >/dev/null
fi

refresh_reports
