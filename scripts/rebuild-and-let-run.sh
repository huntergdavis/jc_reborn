#!/bin/bash
# Full PS1 Rebuild and Let Run - Builds and launches, keeps running until manual kill
# Usage: ./rebuild-and-let-run.sh [noclean]

set -e  # Exit on error

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

cd "$(dirname "$0")/.."  # Change to project root

BOOTMODE_FILE="$PWD/config/ps1/BOOTMODE.TXT"
BOOTMODE_BACKUP=""
BOOT_OVERRIDE=""

prepare_duckstation_test_settings() {
    :
}

restore_duckstation_test_settings() {
    :
}

if [ "${1:-}" = "noclean" ]; then
    BUILD_MODE="noclean"
    shift
else
    BUILD_MODE=""
fi

if [ "$#" -gt 0 ]; then
    BOOT_OVERRIDE="$*"
fi

echo "======================================"
echo "PS1 Full Rebuild and Let Run"
echo "======================================"
echo ""

stage_boot_override() {
    if [ ! -f "$BOOTMODE_FILE" ]; then
        return
    fi

    if [ -z "$BOOT_OVERRIDE" ]; then
        return
    fi

    BOOTMODE_BACKUP="/tmp/ps1-bootmode-$$.txt"
    cp "$BOOTMODE_FILE" "$BOOTMODE_BACKUP"

    printf '%s\n' "$BOOT_OVERRIDE" > "$BOOTMODE_FILE"
    echo "=== Boot override ==="
    echo "$BOOT_OVERRIDE"
    echo ""
}

restore_boot_override() {
    if [ -n "$BOOTMODE_BACKUP" ] && [ -f "$BOOTMODE_BACKUP" ]; then
        cp "$BOOTMODE_BACKUP" "$BOOTMODE_FILE"
        rm -f "$BOOTMODE_BACKUP"
    fi
}

trap 'restore_duckstation_test_settings; restore_boot_override' EXIT

# Step 1: Stage boot override and build executable
stage_boot_override

if [ "$BUILD_MODE" = "noclean" ]; then
    echo "=== Incremental build (noclean mode) ==="
    ./scripts/build-ps1.sh
else
    echo "=== Clean build (default) ==="
    ./scripts/build-ps1.sh clean
fi

echo ""

# Step 2: Create CD image
./scripts/make-cd-image.sh

echo ""

# Step 3: Kill any existing DuckStation instances
echo "=== Cleaning up old DuckStation instances ==="
pkill -9 -f "duckstation" 2>/dev/null || true
sleep 1

# Step 4: Launch DuckStation and let it run
echo "=== Launching DuckStation (will keep running) ==="

SCREENSHOT_DIR="$HOME/.var/app/org.duckstation.DuckStation/config/duckstation/screenshots"
CUE_FILE="$PWD/jcreborn.cue"
CAPTURE_INTERVAL=${PS1_CAPTURE_INTERVAL:-5}
INITIAL_CAPTURE_WAIT=${PS1_INITIAL_CAPTURE_WAIT:-35}
CAPTURE_COUNT=${PS1_CAPTURE_COUNT:-4}

mkdir -p "$SCREENSHOT_DIR"

take_duckstation_screenshot() {
    local out_var="$1"
    local marker="/tmp/.ps1_shot_marker_$$"
    : > "$marker"
    local latest=""
    local window_id=""

    if command -v xdotool >/dev/null 2>&1; then
        window_id=$(xdotool search --onlyvisible --name "DuckStation" 2>/dev/null | tail -1 || true)
        if [ -n "$window_id" ]; then
            xdotool windowactivate --sync "$window_id" 2>/dev/null || true
            sleep 0.5
            xdotool key --window "$window_id" F10 2>/dev/null || true
            sleep 1
        fi
        latest=$(find "$SCREENSHOT_DIR" -name "*.png" -newer "$marker" 2>/dev/null | sort | tail -1)
        if [ -n "$latest" ]; then
            printf -v "$out_var" '%s' "$latest"
            rm -f "$marker"
            return 0
        fi
    fi

    if command -v spectacle >/dev/null 2>&1 && { [ -n "$window_id" ] || [ "${PS1_ALLOW_FALLBACK_CAPTURE:-0}" = "1" ]; }; then
        if [ -n "$window_id" ] && command -v xdotool >/dev/null 2>&1; then
            xdotool windowactivate --sync "$window_id" 2>/dev/null || true
            sleep 0.5
        fi
        local fallback="$SCREENSHOT_DIR/ps1-test-$(date +%Y%m%d-%H%M%S).png"
        spectacle -b -n -a -e -S -d 300 -o "$fallback" >/dev/null 2>&1 || true
        if [ -f "$fallback" ]; then
            printf -v "$out_var" '%s' "$fallback"
            rm -f "$marker"
            return 0
        fi
    fi

    echo "WARNING: DuckStation-native screenshot capture failed; window fallback capture also failed." >&2
    rm -f "$marker"
    return 1
}

# Launch DuckStation with fast boot
prepare_duckstation_test_settings
flatpak run --filesystem="$(dirname "$CUE_FILE")" org.duckstation.DuckStation -fastboot "$CUE_FILE" &
DUCK_PID=$!

echo "DuckStation PID: $DUCK_PID"
echo "Waiting ${INITIAL_CAPTURE_WAIT} seconds for initial screenshot..."

sleep "$INITIAL_CAPTURE_WAIT"

if kill -0 "$DUCK_PID" 2>/dev/null; then
    if take_duckstation_screenshot SCREENSHOT_FILE; then
        echo "Screenshot 1/${CAPTURE_COUNT} saved to: $SCREENSHOT_FILE"
    fi
fi

for ((capture_index=2; capture_index<=CAPTURE_COUNT; capture_index++)); do
    echo "Waiting ${CAPTURE_INTERVAL} more seconds for screenshot ${capture_index}..."
    sleep "$CAPTURE_INTERVAL"
    if kill -0 "$DUCK_PID" 2>/dev/null; then
        if take_duckstation_screenshot SCREENSHOT_FILE2; then
            echo "Screenshot ${capture_index}/${CAPTURE_COUNT} saved to: $SCREENSHOT_FILE2"
        fi
    fi
done

echo ""
echo "======================================"
echo "Build complete! DuckStation running..."
echo "Press Escape in emulator or Ctrl+C here to stop"
echo "Screenshots in: $SCREENSHOT_DIR"
echo "======================================"

# Wait for DuckStation to exit (user must manually close it)
wait $DUCK_PID 2>/dev/null || true

echo "DuckStation closed."
exit 0
