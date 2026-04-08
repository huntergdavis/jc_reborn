#!/bin/bash
# capture-reference-scene-list.sh — Capture seeded reference metadata/frames
# for an explicit list of scenes.

set -euo pipefail

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

SEED=""
FRAMES=4200
START_FRAME=""
INTERVAL=120
OUTPUT_DIR="$PROJECT_ROOT/regtest-references"
SKIP_INDEX=0
SCENE_FILE=""
STATUS_FILTER=""
LIMIT=""
declare -a SCENES=()

usage() {
    cat <<'USAGE'
Usage: capture-reference-scene-list.sh [OPTIONS] [--scene "ADS TAG"...]

Options:
  --scene SPEC     Scene spec, e.g. "BUILDING 1" (repeatable)
  --scene-file F   Read scenes from regtest-style scene list
  --status NAME    Filter scene-file rows by status (e.g. verified)
  --limit N        Limit number of scenes loaded from scene-file
  --seed N         Force deterministic BOOTMODE RNG seed
  --frames N       Frames per scene (default: 4200)
  --start-frame N  First PS1 frame to keep for every scene (default: reviewed per-scene start)
  --interval N     Capture interval (default: 120)
  --output DIR     Reference output root (default: regtest-references/)
  --skip-index     Do not rebuild the reference index after capture
  -h, --help       Show this help
USAGE
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --scene)      SCENES+=("$2"); shift 2 ;;
        --scene-file) SCENE_FILE="$2"; shift 2 ;;
        --status)     STATUS_FILTER="$2"; shift 2 ;;
        --limit)      LIMIT="$2"; shift 2 ;;
        --seed)       SEED="$2"; shift 2 ;;
        --frames)     FRAMES="$2"; shift 2 ;;
        --start-frame) START_FRAME="$2"; shift 2 ;;
        --interval)   INTERVAL="$2"; shift 2 ;;
        --output)     OUTPUT_DIR="$2"; shift 2 ;;
        --skip-index) SKIP_INDEX=1; shift ;;
        -h|--help)    usage ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if [ -n "$SCENE_FILE" ]; then
    mapfile -t FILE_SCENES < <(python3 - "$SCENE_FILE" "$STATUS_FILTER" "$LIMIT" <<'PY'
import sys
from pathlib import Path

scene_file = Path(sys.argv[1])
status_filter = sys.argv[2]
limit_raw = sys.argv[3]
limit = int(limit_raw) if limit_raw else None

count = 0
for raw in scene_file.read_text(encoding="utf-8").splitlines():
    line = raw.split("#", 1)[0].strip()
    if not line:
        continue
    parts = line.split()
    if len(parts) < 4:
        continue
    ads_name, tag, _scene_index, status = parts[:4]
    if status_filter and status != status_filter:
        continue
    print(f"{ads_name} {tag}")
    count += 1
    if limit is not None and count >= limit:
        break
PY
)
    SCENES+=("${FILE_SCENES[@]}")
fi

if [ ${#SCENES[@]} -eq 0 ]; then
    echo "ERROR: At least one scene must come from --scene or --scene-file." >&2
    usage
fi

mkdir -p "$OUTPUT_DIR"

for scene in "${SCENES[@]}"; do
    echo "== capture $scene =="
    CMD=(
        "$SCRIPT_DIR/capture-reference-frames.sh"
        --scene "$scene"
        --frames "$FRAMES"
        --interval "$INTERVAL"
        --skip-specials
        --output "$OUTPUT_DIR"
    )
    if [ -n "$START_FRAME" ]; then
        CMD+=(--start-frame "$START_FRAME")
    fi
    if [ -n "$SEED" ]; then
        CMD+=(--seed "$SEED")
    fi
    "${CMD[@]}"
    echo
done

if [ "$SKIP_INDEX" -eq 0 ]; then
    echo "== build index =="
    "$SCRIPT_DIR/build-reference-index.py" --refdir "$OUTPUT_DIR"
fi
