#!/bin/bash
# Run DuckStation regtest on a PS1 CD image inside Docker.
#
# The regtest binary runs the game headlessly (no display), captures
# frames as PNGs at a configurable interval, and forwards all PS1
# printf/TTY output to stdout.
#
# Usage:
#   ./scripts/run-regtest.sh [options]
#
# Examples:
#   # Run for 30 seconds, capture 1 frame/sec, auto-detect .cue
#   ./scripts/run-regtest.sh
#
#   # Run a specific .cue for 60 seconds, dump every 30th frame
#   ./scripts/run-regtest.sh --cue build-ps1/jcreborn.cue --frames 3600 --dumpinterval 30
#
#   # Run with explicit BIOS directory
#   ./scripts/run-regtest.sh --bios ~/ps1-bios/
#
# Requires:
#   - Docker image jc-reborn-regtest:latest (build with ./scripts/build-regtest-image.sh)
#   - A PS1 BIOS image (scph1001.bin or similar) in the BIOS directory
#   - A .cue/.bin CD image to test

set -euo pipefail

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

cd "$(dirname "$0")/.."  # project root

# Source shared regtest configuration for defaults.
if [ -f "config/ps1/regtest-config.sh" ]; then
    # shellcheck source=../config/ps1/regtest-config.sh
    source "config/ps1/regtest-config.sh"
fi

# ── Defaults ────────────────────────────────────────────────────────────────

FRAMES="${REGTEST_FRAMES:-1800}"        # 1800 frames = 30 sec at 60fps
INTERVAL="${REGTEST_INTERVAL:-60}"      # capture every 60th frame = 1/sec
OUTPUT_DIR="${REGTEST_OUTPUT_DIR:-regtest-results}"
RENDERER="Software"
CUE_FILE=""
BIOS_DIR=""
LOG_LEVEL="Info"
IMAGE_TAG="jc-reborn-regtest:latest"
EXTRA_ARGS=()
TIMEOUT="${REGTEST_TIMEOUT:-120}"
UPSCALE=""
CPU_MODE=""

# ── Argument Parsing ────────────────────────────────────────────────────────

usage() {
    cat <<'USAGE'
Usage: run-regtest.sh [options]

Options:
  --frames N          Run for N frames (default: 1800 = 30s at 60fps)
  --dumpinterval N    Capture every Nth frame (default: 60 = 1 per second)
  --dumpdir DIR       Output directory for frames and logs (default: regtest-results/)
  --cue FILE          Path to .cue file (default: auto-detect jcreborn.cue)
  --bios DIR          Path to directory containing PS1 BIOS files
  --renderer NAME     Renderer: Software, Vulkan, OpenGL (default: Software)
  --log LEVEL         Log level: Error, Warning, Info, Verbose, Debug (default: Info)
  --timeout SECS      Wall-clock timeout per run in seconds (default: 120)
  --upscale N         Upscale resolution multiplier (e.g., 2, 3)
  --cpu MODE          CPU execution mode (e.g., Interpreter, CachedInterpreter, Recompiler)
  --image TAG         Docker image tag (default: jc-reborn-regtest:latest)
  --help              Show this help message

Environment variables (from config/ps1/regtest-config.sh):
  REGTEST_FRAMES      Default frame count
  REGTEST_INTERVAL    Default dump interval
  REGTEST_OUTPUT_DIR  Default output directory
  REGTEST_TIMEOUT     Default timeout
USAGE
}

while [ $# -gt 0 ]; do
    case "$1" in
        --frames)
            FRAMES="$2"; shift 2 ;;
        --dumpinterval)
            INTERVAL="$2"; shift 2 ;;
        --dumpdir)
            OUTPUT_DIR="$2"; shift 2 ;;
        --cue)
            CUE_FILE="$2"; shift 2 ;;
        --bios)
            BIOS_DIR="$2"; shift 2 ;;
        --renderer)
            RENDERER="$2"; shift 2 ;;
        --log)
            LOG_LEVEL="$2"; shift 2 ;;
        --timeout)
            TIMEOUT="$2"; shift 2 ;;
        --upscale)
            UPSCALE="$2"; shift 2 ;;
        --cpu)
            CPU_MODE="$2"; shift 2 ;;
        --image)
            IMAGE_TAG="$2"; shift 2 ;;
        --help|-h)
            usage; exit 0 ;;
        --)
            shift; EXTRA_ARGS+=("$@"); break ;;
        *)
            echo "Unknown option: $1" >&2
            echo "Run '$0 --help' for usage." >&2
            exit 1 ;;
    esac
done

# ── Auto-detect .cue file ──────────────────────────────────────────────────

if [ -z "$CUE_FILE" ]; then
    # Try common locations.
    for candidate in \
        "jcreborn.cue" \
        "build-ps1/jcreborn.cue" \
        "cd-image/jcreborn.cue"; do
        if [ -f "$candidate" ]; then
            CUE_FILE="$candidate"
            break
        fi
    done

    if [ -z "$CUE_FILE" ]; then
        echo "ERROR: No .cue file found." >&2
        echo "Provide one with --cue or run ./scripts/make-cd-image.sh first." >&2
        exit 1
    fi
fi

CUE_FILE="$(realpath "$CUE_FILE")"
CUE_DIR="$(dirname "$CUE_FILE")"
CUE_NAME="$(basename "$CUE_FILE")"

if [ ! -f "$CUE_FILE" ]; then
    echo "ERROR: CUE file not found: $CUE_FILE" >&2
    exit 1
fi

# ── Auto-detect BIOS directory ─────────────────────────────────────────────

if [ -z "$BIOS_DIR" ]; then
    # Check common BIOS locations.
    for candidate in \
        "$HOME/.local/share/duckstation/bios" \
        "$HOME/.config/duckstation/bios" \
        "$HOME/.var/app/org.duckstation.DuckStation/config/duckstation/bios" \
        "$HOME/ps1-bios" \
        "bios"; do
        if [ -d "$candidate" ]; then
            BIOS_DIR="$candidate"
            break
        fi
    done

    if [ -z "$BIOS_DIR" ]; then
        echo "WARNING: No BIOS directory found." >&2
        echo "DuckStation regtest requires a PS1 BIOS image." >&2
        echo "Provide one with --bios or place it in ~/.local/share/duckstation/bios/" >&2
        echo "Continuing anyway (DuckStation may use HLE BIOS with -fastboot)." >&2
    fi
fi

if [ -n "$BIOS_DIR" ]; then
    BIOS_DIR="$(realpath "$BIOS_DIR")"
fi

# ── Verify Docker image ────────────────────────────────────────────────────

if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
    echo "ERROR: Docker image '$IMAGE_TAG' not found." >&2
    echo "Build it first:  ./scripts/build-regtest-image.sh" >&2
    exit 1
fi

# ── Prepare output directory ───────────────────────────────────────────────

RUN_TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
RUN_OUTPUT_DIR="${OUTPUT_DIR}/${RUN_TIMESTAMP}"
FRAMES_DIR="${RUN_OUTPUT_DIR}/frames"

mkdir -p "$FRAMES_DIR"

# ── Build docker run command ───────────────────────────────────────────────

DOCKER_ARGS=(
    docker run --rm
    --platform linux/amd64
    -v "${CUE_DIR}:/game:ro"
    -v "$(realpath "$RUN_OUTPUT_DIR"):/output"
)

if [ -n "$BIOS_DIR" ] && [ -d "$BIOS_DIR" ]; then
    DOCKER_ARGS+=(-v "${BIOS_DIR}:/bios:ro")
    DOCKER_ARGS+=(-v "${BIOS_DIR}:/root/.local/share/duckstation/bios:ro")
fi

DOCKER_ARGS+=("$IMAGE_TAG")

# Regtest binary arguments.
REGTEST_ARGS=(
    -log "$LOG_LEVEL"
    -console
    -renderer "$RENDERER"
    -frames "$FRAMES"
    -dumpdir /output/frames
    -dumpinterval "$INTERVAL"
)

if [ -n "$UPSCALE" ]; then
    REGTEST_ARGS+=(-upscale "$UPSCALE")
fi

if [ -n "$CPU_MODE" ]; then
    REGTEST_ARGS+=(-cpu "$CPU_MODE")
fi

# Append any extra passthrough arguments.
if [ ${#EXTRA_ARGS[@]} -gt 0 ]; then
    REGTEST_ARGS+=("${EXTRA_ARGS[@]}")
fi

# The game path (inside the container) must come last, after --.
REGTEST_ARGS+=(-- "/game/${CUE_NAME}")

# ── Run ─────────────────────────────────────────────────────────────────────

RUN_SECONDS=$(echo "$FRAMES / 60" | bc 2>/dev/null || echo "?")

echo "======================================"
echo "DuckStation Regtest"
echo "======================================"
echo ""
echo "CUE file:       $CUE_FILE"
echo "BIOS dir:       ${BIOS_DIR:-<not set>}"
echo "Frames:         $FRAMES (~${RUN_SECONDS}s at 60fps)"
echo "Dump interval:  every ${INTERVAL} frames"
echo "Renderer:       $RENDERER"
echo "Output dir:     $RUN_OUTPUT_DIR"
echo "Timeout:        ${TIMEOUT}s"
echo ""

LOG_FILE="${RUN_OUTPUT_DIR}/regtest.log"

# Run with a wall-clock timeout to catch infinite loops.
REGTEST_EXIT=0
if command -v timeout >/dev/null 2>&1; then
    timeout "${TIMEOUT}s" \
        "${DOCKER_ARGS[@]}" "${REGTEST_ARGS[@]}" \
        2>&1 | tee "$LOG_FILE" \
        || REGTEST_EXIT=$?
else
    "${DOCKER_ARGS[@]}" "${REGTEST_ARGS[@]}" \
        2>&1 | tee "$LOG_FILE" \
        || REGTEST_EXIT=$?
fi

# ── Report ──────────────────────────────────────────────────────────────────

echo ""
echo "======================================"
echo "Results"
echo "======================================"

FRAME_COUNT=0
if [ -d "$FRAMES_DIR" ]; then
    FRAME_COUNT=$(find "$FRAMES_DIR" -name "frame_*.png" 2>/dev/null | wc -l)
fi

echo ""
echo "Exit code:      $REGTEST_EXIT"
echo "Frames dumped:  $FRAME_COUNT"
echo "Log file:       $LOG_FILE"
echo "Frame PNGs:     $FRAMES_DIR/"
echo ""

if [ "$REGTEST_EXIT" -eq 124 ]; then
    echo "WARNING: Test was killed by timeout (${TIMEOUT}s wall-clock limit)."
    echo "The game may have hung.  Check the log for details."
elif [ "$REGTEST_EXIT" -ne 0 ]; then
    echo "WARNING: Regtest exited with code $REGTEST_EXIT."
    echo "Check the log for errors."
fi

# Extract PS1 printf/TTY lines from the log (lines from the guest console).
TTY_FILE="${RUN_OUTPUT_DIR}/tty-output.txt"
if [ -f "$LOG_FILE" ]; then
    # DuckStation logs guest TTY with a "TTY:" or "Console:" prefix,
    # or our game's printf output appears on stdout directly.
    # Capture everything that is not a DuckStation host log line.
    grep -v '^\[' "$LOG_FILE" > "$TTY_FILE" 2>/dev/null || true
    TTY_LINES=$(wc -l < "$TTY_FILE" 2>/dev/null || echo 0)
    if [ "$TTY_LINES" -gt 0 ]; then
        echo "PS1 TTY output:  $TTY_FILE ($TTY_LINES lines)"
    fi
fi

echo ""
exit "$REGTEST_EXIT"
