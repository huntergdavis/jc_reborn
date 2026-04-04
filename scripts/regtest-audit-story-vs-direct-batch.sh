#!/bin/bash
# regtest-audit-story-vs-direct-batch.sh — Run seeded story-vs-direct audits
# across a list of scenes and summarize ENTRY/LASTSCN parity.

set -euo pipefail

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

SEED=1
FRAMES=4200
INTERVAL=120
SKIP_BUILD=0
OUTPUT_ROOT="$PROJECT_ROOT/regtest-results"
SUMMARY_FILE=""
declare -a SCENES=()

usage() {
    cat <<'USAGE'
Usage: regtest-audit-story-vs-direct-batch.sh [OPTIONS] --scene "ADS TAG" [--scene "ADS TAG"...]

Options:
  --scene SPEC     Scene spec, e.g. "BUILDING 1" (repeatable)
  --seed N         Force BOOTMODE RNG seed (default: 1)
  --frames N       Frames per run (default: 4200)
  --interval N     Capture interval (default: 120)
  --output DIR     Output root (default: regtest-results/)
  --summary FILE   Optional JSON summary output path
  --skip-build     Pass through to underlying regtests
  -h, --help       Show this help
USAGE
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --scene)    SCENES+=("$2"); shift 2 ;;
        --seed)     SEED="$2"; shift 2 ;;
        --frames)   FRAMES="$2"; shift 2 ;;
        --interval) INTERVAL="$2"; shift 2 ;;
        --output)   OUTPUT_ROOT="$2"; shift 2 ;;
        --summary)  SUMMARY_FILE="$2"; shift 2 ;;
        --skip-build) SKIP_BUILD=1; shift ;;
        -h|--help)  usage ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if [ ${#SCENES[@]} -eq 0 ]; then
    echo "ERROR: At least one --scene is required." >&2
    usage
fi

declare -a SUMMARY_ROWS=()

for scene in "${SCENES[@]}"; do
    read -r ADS_NAME TAG <<< "$scene"
    LABEL_BASE="${ADS_NAME,,}-${TAG}"
    HOLD_DIR="$OUTPUT_ROOT/${LABEL_BASE}-storyhold-audit"
    DIRECT_DIR="$OUTPUT_ROOT/${LABEL_BASE}-storydirect-audit"

    CMD=(
        "$SCRIPT_DIR/regtest-audit-story-vs-direct.sh"
        --scene "$scene"
        --seed "$SEED"
        --frames "$FRAMES"
        --interval "$INTERVAL"
        --output "$OUTPUT_ROOT"
    )
    if [ "$SKIP_BUILD" -eq 1 ]; then
        CMD+=(--skip-build)
    fi

    "${CMD[@]}"

    COMPARE_JSON="$("$SCRIPT_DIR/regtest-compare.sh" --json "$HOLD_DIR" "$DIRECT_DIR")"
    SUMMARY_ROWS+=("$COMPARE_JSON")
done

python3 - <<'PY' "$SUMMARY_FILE" "${SUMMARY_ROWS[@]}"
import json, sys

summary_path = sys.argv[1]
rows = []
for raw in sys.argv[2:]:
    data = json.loads(raw)
    rows.extend(data.get("scenes", []))

payload = {
    "scenes": rows,
    "counts": {},
}
counts = {}
for row in rows:
    counts[row["verdict"]] = counts.get(row["verdict"], 0) + 1
payload["counts"] = counts

print(json.dumps(payload, indent=2))
if summary_path:
    with open(summary_path, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2)
        f.write("\n")
PY
