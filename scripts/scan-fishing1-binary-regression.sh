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

  python3 - "$OUTPUT_ROOT/fishing1-binary-regression-summary.json" <<'PY'
import json
import sys
from pathlib import Path

summary_path = Path(sys.argv[1])
payload = json.loads(summary_path.read_text(encoding="utf-8"))
rows = payload.get("rows") or []
out_path = summary_path.with_name("fishing1-startup-summary.tsv")

with out_path.open("w", encoding="utf-8") as handle:
    handle.write(
        "build\tstartup_regime\tfirst_visible\tfirst_lower_half\tfirst_full_height\t"
        "last_partial_height\tlast_black\tblack\tocean\tisland\tcorrect\tshoe\tmidgap\tstate_hash\n"
    )
    for row in rows:
        cov = row.get("coverage") or {}
        result = row.get("result") or {}
        build = Path(row.get("result_json", "")).parts[-2] if row.get("result_json") else ""
        fields = [
            build,
            row.get("startup_regime", ""),
            str(cov.get("first_visible_frame", "")),
            str(cov.get("first_lower_half_visible_frame", "")),
            str(cov.get("first_full_height_visible_frame", "")),
            str(cov.get("last_partial_height_visible_frame", "")),
            str(cov.get("last_black_frame", "")),
            str(cov.get("unique_black_hashes", "")),
            str(cov.get("unique_ocean_only_hashes", "")),
            str(cov.get("unique_island_only_hashes", "")),
            str(cov.get("unique_correct_hashes", "")),
            str(cov.get("unique_shoe_hashes", "")),
            str(cov.get("unique_post_correct_pre_shoe_hashes", "")),
            str(result.get("state_hash", "")),
        ]
        handle.write("\t".join(fields) + "\n")

print(out_path)
PY

  if [ -n "$BOUNDARY_STARTUP_REGIME" ]; then
    python3 "$PROJECT_ROOT/scripts/find-fishing1-regression-boundary.py" \
      --summary-json "$OUTPUT_ROOT/fishing1-binary-regression-summary.json" \
      --startup-regime "$BOUNDARY_STARTUP_REGIME" \
      > "$OUTPUT_ROOT/fishing1-startup-boundary.json"
    printf '%s\n' "$OUTPUT_ROOT/fishing1-startup-boundary.json"
  fi
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
