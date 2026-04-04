#!/bin/bash
# certify-direct-scene.sh — Build an exact direct-scene certification bundle:
# host deterministic sequence, PS1 regtest deterministic sequence, and
# normalized per-frame comparison between them.

set -euo pipefail

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

SCENE=""
FRAMES=1200
INTERVAL=60
SEED=1
OUTPUT_ROOT="$PROJECT_ROOT/cert-results/direct-scene"
HOST_SEQUENCE=""
MIN_RESULT_COVERAGE="0.50"
REQUIRE_EXACT_MATCH=1
ISLAND_X=""
ISLAND_Y=""
LOWTIDE=""
HOST_ISLAND_X=""
HOST_ISLAND_Y=""
HOST_LOWTIDE=""

apply_scene_defaults() {
    case "$SCENE" in
        "BUILDING 1")
            if [ -z "$ISLAND_X" ] && [ -z "$ISLAND_Y" ]; then
                ISLAND_X=0
                ISLAND_Y=0
            fi
            if [ -z "$LOWTIDE" ]; then
                LOWTIDE=0
            fi
            ;;
    esac
}

usage() {
    cat <<'USAGE'
Usage: certify-direct-scene.sh --scene "ADS TAG" [OPTIONS]

Options:
  --scene "ADS TAG"  Scene label, e.g. "BUILDING 1"
  --frames N         Frame count (default: 1200)
  --interval N       Capture interval (default: 60)
  --seed N           Forced deterministic seed (default: 1)
  --island-x N       Force island X position on both host and PS1
  --island-y N       Force island Y position on both host and PS1
  --lowtide 0|1      Force low tide state on both host and PS1
  --host-sequence P  Reuse an existing host result.json or directory
  --host-island-x N  Force host island X position for reference capture
  --host-island-y N  Force host island Y position for reference capture
  --host-lowtide 0|1 Force host low tide state for reference capture
  --min-result-coverage R
                     Minimum shared-frame coverage on PS1 side (default: 0.50)
  --allow-differences
                     Do not fail if overlapping frames differ
  --output DIR       Output root (default: cert-results/direct-scene/)
  -h, --help         Show this help
USAGE
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --scene) SCENE="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --interval) INTERVAL="$2"; shift 2 ;;
        --seed) SEED="$2"; shift 2 ;;
        --island-x) ISLAND_X="$2"; shift 2 ;;
        --island-y) ISLAND_Y="$2"; shift 2 ;;
        --lowtide) LOWTIDE="$2"; shift 2 ;;
        --host-sequence) HOST_SEQUENCE="$2"; shift 2 ;;
        --host-island-x) HOST_ISLAND_X="$2"; shift 2 ;;
        --host-island-y) HOST_ISLAND_Y="$2"; shift 2 ;;
        --host-lowtide) HOST_LOWTIDE="$2"; shift 2 ;;
        --min-result-coverage) MIN_RESULT_COVERAGE="$2"; shift 2 ;;
        --allow-differences) REQUIRE_EXACT_MATCH=0; shift ;;
        --output) OUTPUT_ROOT="$2"; shift 2 ;;
        -h|--help) usage ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if [ -z "$SCENE" ]; then
    echo "ERROR: --scene is required." >&2
    usage
fi

apply_scene_defaults

if [ -n "$ISLAND_X" ] || [ -n "$ISLAND_Y" ]; then
    if [ -z "$ISLAND_X" ] || [ -z "$ISLAND_Y" ]; then
        echo "ERROR: --island-x and --island-y must be provided together." >&2
        exit 1
    fi
fi
if [ -z "$HOST_ISLAND_X" ] && [ -z "$HOST_ISLAND_Y" ] && [ -n "$ISLAND_X" ] && [ -n "$ISLAND_Y" ]; then
    HOST_ISLAND_X="$ISLAND_X"
    HOST_ISLAND_Y="$ISLAND_Y"
fi
if [ -z "$HOST_LOWTIDE" ] && [ -n "$LOWTIDE" ]; then
    HOST_LOWTIDE="$LOWTIDE"
fi

read -r ADS_NAME TAG <<< "$SCENE"
LABEL="${ADS_NAME,,}-${TAG}"
RUN_TS="$(date +%Y%m%d-%H%M%S)"
RUN_DIR="$OUTPUT_ROOT/$LABEL/$RUN_TS"
HOST_DIR="$RUN_DIR/host"
PS1_DIR="$RUN_DIR/ps1"
SUMMARY_JSON="$RUN_DIR/summary.json"
mkdir -p "$HOST_DIR" "$PS1_DIR"

SCENE_INDEX="$(python3 - "$PROJECT_ROOT/config/ps1/regtest-scenes.txt" "$ADS_NAME" "$TAG" <<'PY'
import sys
from pathlib import Path

scene_file = Path(sys.argv[1])
ads_name = sys.argv[2]
tag = sys.argv[3]

for raw in scene_file.read_text(encoding="utf-8").splitlines():
    line = raw.split("#", 1)[0].strip()
    if not line:
        continue
    parts = line.split()
    if len(parts) < 3:
        continue
    if parts[0] == ads_name and parts[1] == tag:
        print(parts[2])
        raise SystemExit(0)
raise SystemExit(1)
PY
)"

if [ -n "$HOST_SEQUENCE" ]; then
    HOST_SEQUENCE_PATH="$HOST_SEQUENCE"
else
    HOST_CAPTURE_CMD=(
        "$PROJECT_ROOT/scripts/capture-host-scene.sh"
        --scene "$SCENE"
        --mode story-direct
        --frames "$FRAMES"
        --interval "$INTERVAL"
        --seed "$SEED"
        --output "$HOST_DIR"
    )
    if [ -n "$HOST_ISLAND_X" ] || [ -n "$HOST_ISLAND_Y" ]; then
        if [ -z "$HOST_ISLAND_X" ] || [ -z "$HOST_ISLAND_Y" ]; then
            echo "ERROR: --host-island-x and --host-island-y must be provided together." >&2
            exit 1
        fi
        HOST_CAPTURE_CMD+=(--island-x "$HOST_ISLAND_X" --island-y "$HOST_ISLAND_Y")
    fi
    if [ -n "$HOST_LOWTIDE" ]; then
        HOST_CAPTURE_CMD+=(--lowtide "$HOST_LOWTIDE")
    fi
    "${HOST_CAPTURE_CMD[@]}" >/dev/null
    HOST_SEQUENCE_PATH="$HOST_DIR"
fi

REGTEST_EXIT=0
if ! "$PROJECT_ROOT/scripts/regtest-scene.sh" \
        --scene "$SCENE" \
        --scene-index "$SCENE_INDEX" \
        --boot "story direct $SCENE_INDEX seed $SEED${ISLAND_X:+ island-pos $ISLAND_X $ISLAND_Y}${LOWTIDE:+ lowtide $LOWTIDE}" \
        --frames "$FRAMES" \
        --interval "$INTERVAL" \
        --output "$PS1_DIR" \
        --quiet \
        >/dev/null; then
    REGTEST_EXIT=$?
fi

if [ ! -f "$PS1_DIR/result.json" ]; then
    echo "ERROR: regtest-scene.sh failed before producing result.json (exit $REGTEST_EXIT)." >&2
    exit "${REGTEST_EXIT:-1}"
fi

python3 "$PROJECT_ROOT/scripts/compare-sequence-runs.py" \
    --json \
    --scene-entry-align \
    --result "$PS1_DIR" \
    --reference "$HOST_SEQUENCE_PATH" \
    > "$SUMMARY_JSON"

python3 - "$PS1_DIR/result.json" "$SUMMARY_JSON" "$MIN_RESULT_COVERAGE" "$REQUIRE_EXACT_MATCH" <<'PY'
import json
import sys
from pathlib import Path

ps1_result = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
summary_path = Path(sys.argv[2])
min_result_coverage = float(sys.argv[3])
require_exact_match = bool(int(sys.argv[4]))

if ps1_result.get("outcome", {}).get("frames_captured", 0) <= 0:
    print("ERROR: exact cert lane captured zero PS1 frames", file=sys.stderr)
    raise SystemExit(2)

summary = json.loads(summary_path.read_text(encoding="utf-8"))
if summary.get("common_frame_count", 0) <= 0:
    print("ERROR: exact cert lane found zero common frame numbers", file=sys.stderr)
    raise SystemExit(3)

coverage = summary.get("result_coverage_ratio")
if coverage is None or coverage < min_result_coverage:
    print(
        f"ERROR: exact cert lane overlap too low on PS1 side: "
        f"{coverage!r} < {min_result_coverage}",
        file=sys.stderr,
    )
    raise SystemExit(4)

if require_exact_match and not summary.get("all_frames_match", False):
    print("ERROR: exact cert lane found pixel differences in overlapping frames", file=sys.stderr)
    raise SystemExit(5)
PY

echo "$SUMMARY_JSON"
