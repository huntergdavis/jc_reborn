#!/bin/bash
# regtest-audit-story-vs-ads.sh — Compare story single boot against direct ADS
#                                  boot using best-scene-frame hashes.
#
# Usage:
#   ./scripts/regtest-audit-story-vs-ads.sh --scene "BUILDING 1"
#   ./scripts/regtest-audit-story-vs-ads.sh --scene "STAND 1" --scene-index 38
#
# The script runs:
#   1. story single <scene_index>
#   2. ads <ADS>.ADS <tag>
# and compares their visual_best.frame_sha256 values via regtest-compare.sh.

set -euo pipefail

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

SCENE_SPEC=""
SCENE_INDEX=""
FRAMES=3000
START_FRAME=""
START_FRAME_EXPLICIT=0
MIN_TAIL_FRAMES="${REGTEST_SCENE_CAPTURE_MIN_TAIL_FRAMES:-1200}"
INTERVAL=120
OUTPUT_ROOT="$PROJECT_ROOT/regtest-results"
SKIP_BUILD=0
SEED=""

usage() {
    cat <<'USAGE'
Usage: regtest-audit-story-vs-ads.sh [OPTIONS] --scene "ADS TAG"

Options:
  --scene SPEC       Scene spec, e.g. "BUILDING 1"
  --scene-index N    Force story scene index (otherwise derived from regtest-scenes.txt)
  --frames N         Frames per run (default: 3000)
  --start-frame N    First PS1 frame to keep (default: reviewed per-scene start)
  --interval N       Capture interval (default: 120)
  --seed N           Force BOOTMODE RNG seed for both runs
  --output DIR       Output root (default: regtest-results/)
  --skip-build       Pass --skip-build (BOOTMODE override still rebuilds disc)
  -h, --help         Show this help
USAGE
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --scene)       SCENE_SPEC="$2"; shift 2 ;;
        --scene-index) SCENE_INDEX="$2"; shift 2 ;;
        --frames)      FRAMES="$2"; shift 2 ;;
        --start-frame) START_FRAME="$2"; START_FRAME_EXPLICIT=1; shift 2 ;;
        --interval)    INTERVAL="$2"; shift 2 ;;
        --seed)        SEED="$2"; shift 2 ;;
        --output)      OUTPUT_ROOT="$2"; shift 2 ;;
        --skip-build)  SKIP_BUILD=1; shift ;;
        -h|--help)     usage ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if [ -z "$SCENE_SPEC" ]; then
    echo "ERROR: --scene is required." >&2
    usage
fi

read -r ADS_NAME TAG <<< "$SCENE_SPEC"
if [ -z "${ADS_NAME:-}" ] || [ -z "${TAG:-}" ]; then
    echo "ERROR: Scene spec must look like \"ADS TAG\"." >&2
    exit 1
fi

if [ -z "$SCENE_INDEX" ]; then
    SCENE_INDEX="$(python3 - <<'PY' "$ADS_NAME" "$TAG" "$PROJECT_ROOT/config/ps1/regtest-scenes.txt"
import sys
ads, tag, path = sys.argv[1], int(sys.argv[2]), sys.argv[3]
with open(path, 'r', encoding='utf-8') as f:
    for raw in f:
        line = raw.split('#', 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) < 4:
            continue
        if parts[0] == ads and int(parts[1]) == tag:
            print(parts[2])
            raise SystemExit(0)
raise SystemExit(1)
PY
)" || {
        echo "ERROR: Could not derive scene index for $SCENE_SPEC from config/ps1/regtest-scenes.txt" >&2
        exit 1
    }
fi

LABEL_BASE="${ADS_NAME,,}-${TAG}"
STORY_DIR="$OUTPUT_ROOT/${LABEL_BASE}-story-audit"
ADS_DIR="$OUTPUT_ROOT/${LABEL_BASE}-ads-audit"
if [ -z "$START_FRAME" ]; then
    START_FRAME="$(python3 "$SCRIPT_DIR/get-scene-capture-start.py" --scene "$SCENE_SPEC")"
fi
if [ "$START_FRAME_EXPLICIT" -eq 0 ]; then
    min_frames=$((START_FRAME + MIN_TAIL_FRAMES))
    if [ "$FRAMES" -lt "$min_frames" ]; then
        FRAMES="$min_frames"
    fi
fi

COMMON_ARGS=(
    --scene "$SCENE_SPEC"
    --frames "$FRAMES"
    --start-frame "$START_FRAME"
    --interval "$INTERVAL"
)
if [ "$SKIP_BUILD" -eq 1 ]; then
    COMMON_ARGS+=(--skip-build)
fi

run_regtest() {
    local label="$1"
    shift
    local rc=0

    set +e
    "$@"
    rc=$?
    set -e

    if [ "$rc" -ne 0 ]; then
        echo "[audit] $label exited with code $rc; preserving artifacts for comparison." >&2
    fi
}

STORY_BOOT="story single $SCENE_INDEX"
ADS_BOOT="ads ${ADS_NAME}.ADS $TAG"
if [ -n "$SEED" ]; then
    STORY_BOOT="$STORY_BOOT seed $SEED"
    ADS_BOOT="$ADS_BOOT seed $SEED"
fi

echo "== story single $SCENE_INDEX =="
run_regtest "story single" "$SCRIPT_DIR/regtest-scene.sh" \
    "${COMMON_ARGS[@]}" \
    --scene-index "$SCENE_INDEX" \
    --boot "$STORY_BOOT" \
    --output "$STORY_DIR"

echo
echo "== ads ${ADS_NAME}.ADS $TAG =="
run_regtest "ads" "$SCRIPT_DIR/regtest-scene.sh" \
    "${COMMON_ARGS[@]}" \
    --scene-index "$SCENE_INDEX" \
    --boot "$ADS_BOOT" \
    --output "$ADS_DIR"

echo
echo "== compare =="
"$SCRIPT_DIR/regtest-compare.sh" "$STORY_DIR" "$ADS_DIR"
