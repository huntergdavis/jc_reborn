#!/bin/bash
# regtest-scene.sh — Run a single PS1 scene through the headless DuckStation
#                    regtest binary and produce structured JSON results.
#
# Usage:
#   ./scripts/regtest-scene.sh --scene "STAND 2" --frames 1800 --output results/stand-2/
#   ./scripts/regtest-scene.sh --scene "JOHNNY 1"
#   ./scripts/regtest-scene.sh --scene "ACTIVITY 4" --scene-index 4
#   ./scripts/regtest-scene.sh --scene "MARY 2" --boot "story scene 33"
#
# The script:
#   1. Writes a temporary BOOTMODE.TXT with the scene override
#   2. Rebuilds the CD image (make-cd-image.sh)
#   3. Runs duckstation-regtest for N frames, capturing screenshots at interval
#   4. Runs decode-ps1-bars.py on captured frames
#   5. Outputs structured JSON to stdout (or --output directory)
#   6. Returns nonzero on failure

set -euo pipefail

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"
REGTEST_SCENE_LIST="$PROJECT_ROOT/config/ps1/regtest-scenes.txt"

# Load shared config
# shellcheck source=../config/ps1/regtest-config.sh
source "$PROJECT_ROOT/config/ps1/regtest-config.sh"

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
SCENE_SPEC=""
BOOT_STRING=""
SCENE_INDEX=""
SCENE_STATUS=""
FRAMES="$REGTEST_FRAMES"
INTERVAL="$REGTEST_INTERVAL"
OUTPUT_DIR=""
SKIP_BUILD=0
QUIET=0
CAPTURE_OVERLAY=0
CAPTURE_OVERLAY_MASK=0
LOG_LEVEL="${REGTEST_LOG_LEVEL:-Info}"

usage() {
    cat <<'USAGE'
Usage: regtest-scene.sh [OPTIONS]

Options:
  --scene SPEC     Scene specification, e.g. "STAND 2" (ADS_NAME TAG)
  --boot STRING    Explicit BOOTMODE command (default: derived from scene)
  --scene-index N  Scene index for story-scene boot and result metadata
  --status NAME    Scene status label for result metadata
  --frames N       Number of emulated frames (default: 1800 = 30s)
  --interval N     Capture a frame every N frames (default: 60 = 1/sec)
  --output DIR     Output directory for results (default: auto-generated)
  --overlay        Append capture-overlay to the boot string
  --overlay-mask   Append capture-overlay-mask to the boot string
  --skip-build     Skip CD image rebuild (use existing jcreborn.cue)
  --quiet          Suppress progress messages on stderr
  -h, --help       Show this help
USAGE
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --scene)     SCENE_SPEC="$2"; shift 2 ;;
        --boot)      BOOT_STRING="$2"; shift 2 ;;
        --scene-index) SCENE_INDEX="$2"; shift 2 ;;
        --status)    SCENE_STATUS="$2"; shift 2 ;;
        --frames)    FRAMES="$2"; shift 2 ;;
        --interval)  INTERVAL="$2"; shift 2 ;;
        --output)    OUTPUT_DIR="$2"; shift 2 ;;
        --overlay)   CAPTURE_OVERLAY=1; shift ;;
        --overlay-mask) CAPTURE_OVERLAY_MASK=1; shift ;;
        --skip-build) SKIP_BUILD=1; shift ;;
        --quiet)     QUIET=1; shift ;;
        -h|--help)   usage ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if [ -z "$SCENE_SPEC" ]; then
    echo "ERROR: --scene is required" >&2
    exit 1
fi

SCENE_LABEL="$SCENE_SPEC"

# Parse scene spec: "STAND 2" => ADS_NAME=STAND TAG=2
read -r ADS_NAME SCENE_TAG <<< "$SCENE_SPEC"
if [ -z "$ADS_NAME" ] || [ -z "$SCENE_TAG" ]; then
    echo "ERROR: Scene spec must be 'ADS_NAME TAG', got: '$SCENE_SPEC'" >&2
    exit 1
fi

lookup_scene_manifest() {
    local ads_name="$1"
    local scene_tag="$2"
    local line
    [ -f "$REGTEST_SCENE_LIST" ] || return 1
    line="$(awk -v ads="$ads_name" -v tag="$scene_tag" '
        $0 !~ /^[[:space:]]*#/ && NF >= 5 && $1 == ads && $2 == tag {
            print;
            exit;
        }
    ' "$REGTEST_SCENE_LIST")"
    [ -n "$line" ] || return 1
    printf '%s\n' "$line"
}

# Build the BOOTMODE override string
if [ -z "$BOOT_STRING" ]; then
    if [ -n "$SCENE_INDEX" ]; then
        BOOT_STRING="story scene ${SCENE_INDEX}"
    elif SCENE_MANIFEST_LINE="$(lookup_scene_manifest "$ADS_NAME" "$SCENE_TAG")"; then
        if [ -z "$SCENE_INDEX" ]; then
            SCENE_INDEX="$(printf '%s\n' "$SCENE_MANIFEST_LINE" | awk '{print $3}')"
        fi
        if [ -z "$SCENE_STATUS" ]; then
            SCENE_STATUS="$(printf '%s\n' "$SCENE_MANIFEST_LINE" | awk '{print $4}')"
        fi
        BOOT_STRING="$(printf '%s\n' "$SCENE_MANIFEST_LINE" | cut -d' ' -f5-)"
    else
        BOOT_STRING="island ads ${ADS_NAME} ${SCENE_TAG}"
    fi
fi

if [ "$CAPTURE_OVERLAY_MASK" -eq 1 ] && [[ "$BOOT_STRING" != *"capture-overlay-mask"* ]]; then
    BOOT_STRING="${BOOT_STRING} capture-overlay-mask"
elif [ "$CAPTURE_OVERLAY" -eq 1 ] && [[ "$BOOT_STRING" != *"capture-overlay"* ]]; then
    BOOT_STRING="${BOOT_STRING} capture-overlay"
fi
if [[ "$BOOT_STRING" != *"capture-meta-dir"* ]]; then
    BOOT_STRING="${BOOT_STRING} capture-meta-dir ps1-meta capture-range 0 ${FRAMES} capture-interval ${INTERVAL} capture-scene-label ${SCENE_LABEL}"
fi

# Output directory
if [ -z "$OUTPUT_DIR" ]; then
    OUTPUT_DIR="${REGTEST_OUTPUT_DIR}/${ADS_NAME,,}-${SCENE_TAG}"
fi
mkdir -p "$OUTPUT_DIR"

log() {
    if [ "$QUIET" -eq 0 ]; then
        echo "[regtest] $*" >&2
    fi
}

# ---------------------------------------------------------------------------
# Check for duckstation-regtest or Docker fallback
# ---------------------------------------------------------------------------
USE_DOCKER_REGTEST=0
if ! command -v "$REGTEST_BIN" >/dev/null 2>&1; then
    if docker image inspect "jc-reborn-regtest:latest" >/dev/null 2>&1; then
        USE_DOCKER_REGTEST=1
        log "Using Dockerized regtest fallback (jc-reborn-regtest:latest)."
    else
        cat >&2 <<EOF
ERROR: '$REGTEST_BIN' not found in PATH, and Docker image 'jc-reborn-regtest:latest' is unavailable.

To build the headless regtest binary, see:
  https://github.com/stenzek/duckstation/blob/master/README.md

You can also set REGTEST_BIN=/path/to/duckstation-regtest in your environment
or in config/ps1/regtest-config.sh, or build the Docker image with:
  ./scripts/build-regtest-image.sh
EOF
        exit 2
    fi
fi

# ---------------------------------------------------------------------------
# Stage BOOTMODE.TXT
# ---------------------------------------------------------------------------
BOOTMODE_FILE="$PROJECT_ROOT/config/ps1/BOOTMODE.TXT"
BOOTMODE_BACKUP=""

stage_bootmode() {
    if [ -f "$BOOTMODE_FILE" ]; then
        BOOTMODE_BACKUP="$(mktemp /tmp/regtest-bootmode-XXXXXX.txt)"
        cp "$BOOTMODE_FILE" "$BOOTMODE_BACKUP"
    fi
    printf '%s\n' "$BOOT_STRING" > "$BOOTMODE_FILE"
    log "BOOTMODE.TXT => $BOOT_STRING"
}

restore_bootmode() {
    if [ -n "$BOOTMODE_BACKUP" ] && [ -f "$BOOTMODE_BACKUP" ]; then
        cp "$BOOTMODE_BACKUP" "$BOOTMODE_FILE"
        rm -f "$BOOTMODE_BACKUP"
    elif [ -f "$BOOTMODE_FILE" ]; then
        : > "$BOOTMODE_FILE"
    fi
}

cleanup() {
    restore_bootmode
    # Kill regtest if still running
    if [ -n "${REGTEST_PID:-}" ] && kill -0 "$REGTEST_PID" 2>/dev/null; then
        kill "$REGTEST_PID" 2>/dev/null || true
        wait "$REGTEST_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Build CD image
# ---------------------------------------------------------------------------
if [ "$SKIP_BUILD" -eq 0 ]; then
    stage_bootmode
    log "Rebuilding PS1 executable..."
    "$PROJECT_ROOT/scripts/build-ps1.sh" >> "$OUTPUT_DIR/build.log" 2>&1
    log "Rebuilding CD image..."
    "$PROJECT_ROOT/scripts/make-cd-image.sh" >> "$OUTPUT_DIR/build.log" 2>&1
    log "CD image built."
else
    stage_bootmode
    log "Skipping build (--skip-build)."
fi

CUE_FILE="$PROJECT_ROOT/jcreborn.cue"
if [ ! -f "$CUE_FILE" ]; then
    echo "ERROR: $CUE_FILE not found. Run make-cd-image.sh first." >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Run duckstation-regtest
# ---------------------------------------------------------------------------
FRAMES_DIR="$OUTPUT_DIR/frames"
if [ "$USE_DOCKER_REGTEST" -eq 0 ]; then
    mkdir -p "$FRAMES_DIR"
fi

REGTEST_LOG="$OUTPUT_DIR/regtest.log"

if [ "$USE_DOCKER_REGTEST" -eq 0 ]; then
    log "Running $REGTEST_BIN for $FRAMES frames (interval=$INTERVAL)..."
    log "Scene: $ADS_NAME tag $SCENE_TAG"

    # duckstation-regtest usage:
    #   duckstation-regtest -- -exe <path> | -disc <path>
    #     -frames <N>            run for N frames then exit
    #     -screenshot-interval <N>  save screenshot every N frames
    #     -screenshot-directory <DIR>
    timeout "$REGTEST_TIMEOUT" "$REGTEST_BIN" \
        -log "$LOG_LEVEL" \
        -console \
        -disc "$CUE_FILE" \
        -frames "$FRAMES" \
        -screenshot-interval "$INTERVAL" \
        -screenshot-directory "$FRAMES_DIR" \
        > "$REGTEST_LOG" 2>&1 &
    REGTEST_PID=$!

    REGTEST_EXIT=0
    wait "$REGTEST_PID" || REGTEST_EXIT=$?
    REGTEST_PID=""

    if [ "$REGTEST_EXIT" -eq 124 ]; then
        log "WARNING: regtest timed out after ${REGTEST_TIMEOUT}s"
    elif [ "$REGTEST_EXIT" -ne 0 ]; then
        log "WARNING: regtest exited with code $REGTEST_EXIT"
    fi
else
    log "Running Dockerized regtest for $FRAMES frames (interval=$INTERVAL)..."
    log "Scene: $ADS_NAME tag $SCENE_TAG"

    REGTEST_EXIT=0
    "$PROJECT_ROOT/scripts/run-regtest.sh" \
        --frames "$FRAMES" \
        --dumpinterval "$INTERVAL" \
        --dumpdir "$OUTPUT_DIR" \
        --log "$LOG_LEVEL" \
        --timeout "$REGTEST_TIMEOUT" \
        > "$REGTEST_LOG" 2>&1 || REGTEST_EXIT=$?

    DOCKER_RUN_DIR="$(find "$OUTPUT_DIR" -mindepth 1 -maxdepth 1 -type d -exec test -f '{}/regtest.log' ';' -print | sort | tail -1)"
    if [ -n "$DOCKER_RUN_DIR" ] && [ -d "$DOCKER_RUN_DIR" ]; then
        FRAMES_DIR="$DOCKER_RUN_DIR/frames"
        if [ -f "$DOCKER_RUN_DIR/regtest.log" ]; then
            REGTEST_LOG="$DOCKER_RUN_DIR/regtest.log"
        fi
    fi

    if [ "$REGTEST_EXIT" -eq 124 ]; then
        log "WARNING: Dockerized regtest timed out after ${REGTEST_TIMEOUT}s"
    elif [ "$REGTEST_EXIT" -ne 0 ]; then
        log "WARNING: Dockerized regtest exited with code $REGTEST_EXIT"
    fi
fi

# ---------------------------------------------------------------------------
# Count captured frames
# ---------------------------------------------------------------------------
FRAME_COUNT=0
FRAME_FILES=()
if [ -d "$FRAMES_DIR" ]; then
    while IFS= read -r -d '' f; do
        FRAME_FILES+=("$f")
    done < <(find "$FRAMES_DIR" -type f -name "*.png" -print0 2>/dev/null | sort -z)
    FRAME_COUNT=${#FRAME_FILES[@]}
fi

if [ "$FRAME_COUNT" -gt 0 ]; then
    FRAMES_DIR="$(dirname "${FRAME_FILES[0]}")"
fi

log "Captured $FRAME_COUNT frame(s)."

# ---------------------------------------------------------------------------
# Run telemetry decode on captured frames
# ---------------------------------------------------------------------------
TELEMETRY_FILE="$OUTPUT_DIR/telemetry.json"
DECODE_SCRIPT="$PROJECT_ROOT/scripts/decode-ps1-bars.py"

if [ "$FRAME_COUNT" -gt 0 ] && [ -x "$DECODE_SCRIPT" ]; then
    log "Decoding telemetry bars..."
    python3 "$DECODE_SCRIPT" --json --include-zero "${FRAME_FILES[@]}" \
        > "$TELEMETRY_FILE" 2>/dev/null || true
else
    echo "[]" > "$TELEMETRY_FILE"
fi

# ---------------------------------------------------------------------------
# Extract printf output from regtest log
# ---------------------------------------------------------------------------
PRINTF_FILE="$OUTPUT_DIR/printf.log"
if [ -f "$REGTEST_LOG" ]; then
    cp "$REGTEST_LOG" "$PRINTF_FILE"
else
    : > "$PRINTF_FILE"
fi

# ---------------------------------------------------------------------------
# Extract PS1 capture metadata sidecars from printf output
# ---------------------------------------------------------------------------
FRAME_META_DIR="$OUTPUT_DIR/frame-meta"
mkdir -p "$FRAME_META_DIR"
python3 - <<'PY' "$PRINTF_FILE" "$FRAME_META_DIR" "$FRAMES_DIR"
import json
import re
import sys
from pathlib import Path

printf_path = Path(sys.argv[1])
meta_dir = Path(sys.argv[2])
frames_dir = Path(sys.argv[3])
pattern = re.compile(r"PS1_CAPTURE_META (\{.*\})")

if printf_path.is_file():
    for line in printf_path.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = pattern.search(line)
        if not match:
            continue
        try:
            payload = json.loads(match.group(1))
        except Exception:
            continue
        frame_number = int(payload.get("frame_number", -1))
        if frame_number < 0:
            continue
        payload["image_path"] = str((frames_dir / f"frame_{frame_number:05d}.png").resolve())
        out_path = meta_dir / f"frame_{frame_number:05d}.json"
        out_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
PY

# ---------------------------------------------------------------------------
# Compute a simple state hash from the last captured frame
# ---------------------------------------------------------------------------
STATE_HASH=""
if [ "$FRAME_COUNT" -gt 0 ]; then
    LAST_FRAME="${FRAME_FILES[$((FRAME_COUNT - 1))]}"
    if command -v sha256sum >/dev/null 2>&1; then
        STATE_HASH="$(sha256sum "$LAST_FRAME" | cut -d' ' -f1)"
    elif command -v shasum >/dev/null 2>&1; then
        STATE_HASH="$(shasum -a 256 "$LAST_FRAME" | cut -d' ' -f1)"
    fi
fi

# ---------------------------------------------------------------------------
# Build structured JSON result
# ---------------------------------------------------------------------------
# Check for crash indicators in printf log
HAS_FATAL=0
if [ -f "$PRINTF_FILE" ]; then
    if grep -qiE '(fatalError|FATAL|panic|assert|abort|crash)' "$PRINTF_FILE" 2>/dev/null; then
        HAS_FATAL=1
    fi
fi

RESULT_FILE="$OUTPUT_DIR/result.json"
python3 -c "
import json, sys, os

result = {
    'scene': {
        'ads_name': '$ADS_NAME',
        'tag': $SCENE_TAG,
        'scene_index': ${SCENE_INDEX:-None},
        'status': '$SCENE_STATUS' if '$SCENE_STATUS' else None,
        'boot_string': '$BOOT_STRING',
    },
    'config': {
        'frames': $FRAMES,
        'interval': $INTERVAL,
        'timeout': $REGTEST_TIMEOUT,
    },
    'outcome': {
        'exit_code': $REGTEST_EXIT,
        'timed_out': $REGTEST_EXIT == 124,
        'frames_captured': $FRAME_COUNT,
        'state_hash': '$STATE_HASH' if '$STATE_HASH' else None,
        'has_fatal_error': bool($HAS_FATAL),
    },
    'paths': {
        'output_dir': os.path.abspath('$OUTPUT_DIR'),
        'frames_dir': os.path.abspath('$FRAMES_DIR'),
        'telemetry': os.path.abspath('$TELEMETRY_FILE'),
        'printf_log': os.path.abspath('$PRINTF_FILE'),
        'build_log': os.path.abspath('$OUTPUT_DIR/build.log'),
    },
}

# Merge telemetry summary if available
telemetry_path = '$TELEMETRY_FILE'
if os.path.isfile(telemetry_path) and os.path.getsize(telemetry_path) > 2:
    try:
        with open(telemetry_path) as f:
            telem = json.load(f)
        if isinstance(telem, list):
            result['telemetry_frames'] = len(telem)
        elif isinstance(telem, dict):
            result['telemetry_frames'] = 1
            telem = [telem]
        # Extract drop stats from last frame
        last = telem[-1] if telem else {}
        rows = last.get('rows', [])
        drops = {r['key']: r['width'] for r in rows if r.get('key', '').startswith('drop_')}
        result['outcome']['drop_indicators'] = drops
    except Exception:
        pass

json.dump(result, sys.stdout, indent=2)
print()
" > "$RESULT_FILE"

log "Results written to $OUTPUT_DIR/result.json"

# Print result JSON to stdout
cat "$RESULT_FILE"

# Return nonzero if the test had issues
if [ "$REGTEST_EXIT" -ne 0 ] || [ "$HAS_FATAL" -ne 0 ] || [ "$FRAME_COUNT" -eq 0 ]; then
    exit 1
fi

exit 0
