#!/bin/bash
# capture-duckstation-scene.sh — Launch real Flatpak DuckStation, capture a
# screenshot series, normalize frames to a common 640x480 scene surface, and
# compare them against a host reference.

set -euo pipefail

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

SCRATCH_DIR="$PROJECT_ROOT/scratch"
mkdir -p "$SCRATCH_DIR"

SCENE=""
BOOT=""
REFERENCE=""
REFERENCE_SEQUENCE=""
OUTPUT_DIR="$PROJECT_ROOT/duckstation-results/scene"
INITIAL_WAIT=35
INTERVAL=5
COUNT=4
BUILD_MODE="noclean"
RENDERER="${PS1_DUCK_RENDERER:-Software}"
SCREENSHOT_MODE="${PS1_DUCK_SCREENSHOT_MODE:-InternalResolution}"
CAPTURE_OVERLAY=0

usage() {
    cat <<'USAGE'
Usage: capture-duckstation-scene.sh --scene "ADS TAG" [OPTIONS]

Options:
  --scene "ADS TAG"     Scene label, e.g. "BUILDING 1"
  --boot STRING         Explicit BOOTMODE override
  --reference PATH      Reference metadata.json or containing directory
  --reference-sequence P Host scene result.json or containing directory
  --output DIR          Output directory root
  --initial-wait N      Seconds before first screenshot (default: 35)
  --interval N          Seconds between screenshots (default: 5)
  --count N             Screenshot count (default: 4)
  --clean               Force clean build
  --renderer NAME       DuckStation renderer (default: Software)
  --screenshot-mode M   DuckStation ScreenshotMode (default: InternalResolution)
  --overlay             Enable machine-readable capture overlay in PS1 screenshots
  -h, --help            Show this help
USAGE
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --scene) SCENE="$2"; shift 2 ;;
        --boot) BOOT="$2"; shift 2 ;;
        --reference) REFERENCE="$2"; shift 2 ;;
        --reference-sequence) REFERENCE_SEQUENCE="$2"; shift 2 ;;
        --output) OUTPUT_DIR="$2"; shift 2 ;;
        --initial-wait) INITIAL_WAIT="$2"; shift 2 ;;
        --interval) INTERVAL="$2"; shift 2 ;;
        --count) COUNT="$2"; shift 2 ;;
        --clean) BUILD_MODE="clean"; shift ;;
        --renderer) RENDERER="$2"; shift 2 ;;
        --screenshot-mode) SCREENSHOT_MODE="$2"; shift 2 ;;
        --overlay) CAPTURE_OVERLAY=1; shift ;;
        -h|--help) usage ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if [ -z "$SCENE" ]; then
    echo "ERROR: --scene is required." >&2
    usage
fi

read -r ADS_NAME TAG <<< "$SCENE"
LABEL="${ADS_NAME}-${TAG}"

if [ -z "$REFERENCE" ]; then
    REFERENCE="$PROJECT_ROOT/host-references/$LABEL"
fi

REFERENCE_META="$REFERENCE"
if [ -d "$REFERENCE_META" ]; then
    REFERENCE_META="$REFERENCE_META/metadata.json"
fi

if [ ! -f "$REFERENCE_META" ]; then
    echo "ERROR: reference metadata not found: $REFERENCE_META" >&2
    exit 1
fi

RUN_TS="$(date +%Y%m%d-%H%M%S)"
RUN_DIR="$OUTPUT_DIR/$LABEL/$RUN_TS"
RAW_DIR="$RUN_DIR/raw"
NORM_DIR="$RUN_DIR/normalized"
COMPARE_DIR="$RUN_DIR/compare"
mkdir -p "$RAW_DIR" "$NORM_DIR" "$COMPARE_DIR"

BOOTMODE_FILE="$PROJECT_ROOT/config/ps1/BOOTMODE.TXT"
BOOTMODE_BACKUP=""
DUCK_SETTINGS="$HOME/.var/app/org.duckstation.DuckStation/config/duckstation/settings.ini"
DUCK_SETTINGS_BACKUP=""
SCREENSHOT_DIR="$HOME/.var/app/org.duckstation.DuckStation/config/duckstation/screenshots"
CUE_FILE="$PROJECT_ROOT/jcreborn.cue"

restore_boot_override() {
    if [ -n "$BOOTMODE_BACKUP" ] && [ -f "$BOOTMODE_BACKUP" ]; then
        cp "$BOOTMODE_BACKUP" "$BOOTMODE_FILE"
        rm -f "$BOOTMODE_BACKUP"
    fi
}

restore_duckstation_settings() {
    if [ -n "$DUCK_SETTINGS_BACKUP" ] && [ -f "$DUCK_SETTINGS_BACKUP" ]; then
        cp "$DUCK_SETTINGS_BACKUP" "$DUCK_SETTINGS"
        rm -f "$DUCK_SETTINGS_BACKUP"
    fi
}

cleanup() {
    restore_duckstation_settings
    restore_boot_override
    if [ -n "${DUCK_PID:-}" ] && kill -0 "$DUCK_PID" 2>/dev/null; then
        kill "$DUCK_PID" 2>/dev/null || true
        wait "$DUCK_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

cp "$BOOTMODE_FILE" "$SCRATCH_DIR/duck-bootmode-$RUN_TS.txt"
BOOTMODE_BACKUP="$SCRATCH_DIR/duck-bootmode-$RUN_TS.txt"
if [ -n "$BOOT" ]; then
    if [ "$CAPTURE_OVERLAY" -eq 1 ] && [[ "$BOOT" != *"capture-overlay"* ]]; then
        BOOT="$BOOT capture-overlay"
    fi
    printf '%s\n' "$BOOT" > "$BOOTMODE_FILE"
else
    if [ "$CAPTURE_OVERLAY" -eq 1 ]; then
        printf 'ads %s.ADS %s capture-overlay\n' "$ADS_NAME" "$TAG" > "$BOOTMODE_FILE"
    else
        printf 'ads %s.ADS %s\n' "$ADS_NAME" "$TAG" > "$BOOTMODE_FILE"
    fi
fi

if [ "$BUILD_MODE" = "clean" ]; then
    ./scripts/build-ps1.sh clean
else
    ./scripts/build-ps1.sh
fi
./scripts/make-cd-image.sh

mkdir -p "$SCREENSHOT_DIR"
cp "$DUCK_SETTINGS" "$SCRATCH_DIR/duck-settings-$RUN_TS.ini"
DUCK_SETTINGS_BACKUP="$SCRATCH_DIR/duck-settings-$RUN_TS.ini"

python3 - "$DUCK_SETTINGS" "$RENDERER" "$SCREENSHOT_MODE" <<'PY'
import configparser
import sys
from pathlib import Path

settings = Path(sys.argv[1])
renderer = sys.argv[2]
screenshot_mode = sys.argv[3]

cp = configparser.ConfigParser()
cp.optionxform = str
cp.read(settings, encoding="utf-8")
for section in ("Main", "GPU", "Display"):
    if section not in cp:
        cp[section] = {}
cp["Main"]["StartFullscreen"] = "true"
cp["GPU"]["Renderer"] = renderer
cp["Display"]["ScreenshotMode"] = screenshot_mode
with settings.open("w", encoding="utf-8") as f:
    cp.write(f)
PY

take_screenshot() {
    local out_file="$1"
    local marker="$SCRATCH_DIR/.duck-shot-$RUN_TS"
    : > "$marker"
    local latest=""
    local window_id=""
    local fallback="$SCRATCH_DIR/duck-shot-${RUN_TS}-$$.png"

    window_id=$(xdotool search --onlyvisible --name "DuckStation" 2>/dev/null | tail -1 || true)
    if [ -n "$window_id" ]; then
        xdotool windowactivate --sync "$window_id" 2>/dev/null || true
        sleep 0.5
        xdotool key --window "$window_id" F10 2>/dev/null || true
        sleep 1
    fi

    latest=$(find "$SCREENSHOT_DIR" -name "*.png" -newer "$marker" 2>/dev/null | sort | tail -1)
    if [ -n "$latest" ]; then
        cp "$latest" "$out_file"
        rm -f "$marker"
        return 0
    fi

    if command -v spectacle >/dev/null 2>&1; then
        if [ -n "$window_id" ]; then
            spectacle -b -n -a -e -S -d 300 -o "$fallback" >/dev/null 2>&1 || true
        else
            spectacle -b -n -f -d 300 -o "$fallback" >/dev/null 2>&1 || true
        fi
        if [ -f "$fallback" ]; then
            cp "$fallback" "$out_file"
            rm -f "$fallback" "$marker"
            return 0
        fi
    fi

    rm -f "$marker"
    rm -f "$fallback"
    return 1
}

flatpak run --filesystem="$(dirname "$CUE_FILE")" org.duckstation.DuckStation -fullscreen "$CUE_FILE" &
DUCK_PID=$!

sleep "$INITIAL_WAIT"

for idx in $(seq 1 "$COUNT"); do
    raw_file="$RAW_DIR/shot_$(printf '%02d' "$idx").png"
    norm_file="$NORM_DIR/shot_$(printf '%02d' "$idx").png"
    compare_file="$COMPARE_DIR/shot_$(printf '%02d' "$idx").json"

    if ! kill -0 "$DUCK_PID" 2>/dev/null; then
        break
    fi
    if ! take_screenshot "$raw_file"; then
        echo "WARNING: failed to capture screenshot $idx" >&2
        break
    fi

    python3 "$PROJECT_ROOT/scripts/normalize-scene-frame.py" "$raw_file" --output "$norm_file" --json \
        > "$COMPARE_DIR/shot_$(printf '%02d' "$idx").normalize.json"
    python3 "$PROJECT_ROOT/scripts/compare-scene-reference.py" --json \
        --result-frame "$raw_file" \
        --reference "$REFERENCE_META" \
        > "$compare_file"
    if [ -n "$REFERENCE_SEQUENCE" ]; then
        python3 "$PROJECT_ROOT/scripts/compare-scene-sequence.py" --json \
            --image "$raw_file" \
            --sequence "$REFERENCE_SEQUENCE" \
            > "$COMPARE_DIR/shot_$(printf '%02d' "$idx").sequence.json"
    fi

    if [ "$idx" -lt "$COUNT" ]; then
        sleep "$INTERVAL"
    fi
done

python3 - "$RUN_DIR" "$REFERENCE_META" "$SCENE" <<'PY'
import json
import sys
from pathlib import Path

run_dir = Path(sys.argv[1])
reference_meta = Path(sys.argv[2])
scene = sys.argv[3]
compare_dir = run_dir / "compare"
rows = []
for path in sorted(compare_dir.glob("shot_[0-9][0-9].json")):
    stem = path.stem
    compare = json.loads(path.read_text(encoding="utf-8"))
    normalize_path = compare_dir / f"{stem}.normalize.json"
    sequence_path = compare_dir / f"{stem}.sequence.json"
    normalize = {}
    sequence = {}
    if normalize_path.is_file():
        normalize = json.loads(normalize_path.read_text(encoding="utf-8"))
    if sequence_path.is_file():
        sequence = json.loads(sequence_path.read_text(encoding="utf-8"))
    rows.append({
        "shot": stem,
        "compare": compare,
        "normalize": normalize,
        "sequence_compare": sequence,
    })

payload = {
    "scene": scene,
    "reference": str(reference_meta),
    "captures": rows,
}
(run_dir / "summary.json").write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
print(run_dir / "summary.json")
PY
