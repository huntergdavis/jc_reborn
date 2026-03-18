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

if [ "$1" = "noclean" ]; then
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

    BOOTMODE_BACKUP="/tmp/ps1-bootmode-$$.txt"
    cp "$BOOTMODE_FILE" "$BOOTMODE_BACKUP"

    if [ -n "$BOOT_OVERRIDE" ]; then
        printf '%s\n' "$BOOT_OVERRIDE" > "$BOOTMODE_FILE"
        echo "=== Boot override ==="
        echo "$BOOT_OVERRIDE"
        echo ""
    fi
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
DUCK_SETTINGS="$HOME/.var/app/org.duckstation.DuckStation/config/duckstation/settings.ini"
CUE_FILE="$PWD/jcreborn.cue"
CAPTURE_INTERVAL=${PS1_CAPTURE_INTERVAL:-5}
DUCK_SETTINGS_BACKUP=""

mkdir -p "$SCREENSHOT_DIR"

prepare_duckstation_test_settings() {
    if [ ! -f "$DUCK_SETTINGS" ]; then
        return
    fi

    DUCK_SETTINGS_BACKUP="/tmp/duckstation-settings-$$.ini"
    cp "$DUCK_SETTINGS" "$DUCK_SETTINGS_BACKUP"

    if grep -q '^StartFullscreen = ' "$DUCK_SETTINGS"; then
        sed -i 's/^StartFullscreen = .*/StartFullscreen = true/' "$DUCK_SETTINGS"
    else
        printf '\nStartFullscreen = true\n' >> "$DUCK_SETTINGS"
    fi
}

restore_duckstation_test_settings() {
    if [ -n "$DUCK_SETTINGS_BACKUP" ] && [ -f "$DUCK_SETTINGS_BACKUP" ]; then
        cp "$DUCK_SETTINGS_BACKUP" "$DUCK_SETTINGS"
        rm -f "$DUCK_SETTINGS_BACKUP"
    fi
}

take_duckstation_screenshot() {
    local out_var="$1"
    local marker="/tmp/.ps1_shot_marker_$$"
    : > "$marker"

    if command -v xdotool >/dev/null 2>&1; then
        xdotool search --name "DuckStation" windowactivate --sync key --window %@ F10 2>/dev/null || true
        sleep 1
        local latest
        latest=$(find "$SCREENSHOT_DIR" -name "*.png" -newer "$marker" 2>/dev/null | sort | tail -1)
        if [ -n "$latest" ]; then
            printf -v "$out_var" '%s' "$latest"
            rm -f "$marker"
            return 0
        fi
    fi

    if command -v spectacle >/dev/null 2>&1; then
        local fallback="$SCREENSHOT_DIR/ps1-test-$(date +%Y%m%d-%H%M%S).png"
        spectacle -b -n -a -e -S -o "$fallback"
        printf -v "$out_var" '%s' "$fallback"
        rm -f "$marker"
        return 0
    fi

    rm -f "$marker"
    return 1
}

# Launch DuckStation with fast boot
prepare_duckstation_test_settings
flatpak run --filesystem="$(dirname "$CUE_FILE")" org.duckstation.DuckStation -fastboot "$CUE_FILE" &
DUCK_PID=$!

echo "DuckStation PID: $DUCK_PID"
echo "Waiting 25 seconds for initial screenshot..."

sleep 25

if kill -0 "$DUCK_PID" 2>/dev/null; then
    if take_duckstation_screenshot SCREENSHOT_FILE; then
        echo "Screenshot 1 saved to: $SCREENSHOT_FILE"

        if [ -x "$PWD/scripts/decode-ps1-bars.py" ]; then
            echo ""
            echo "=== Telemetry decode (capture 1/3) ==="
            "$PWD/scripts/decode-ps1-bars.py" --include-zero "$SCREENSHOT_FILE" || true
        fi
    fi
fi

for capture_index in 2 3; do
    echo "Waiting ${CAPTURE_INTERVAL} more seconds for screenshot ${capture_index}..."
    sleep "$CAPTURE_INTERVAL"
    if kill -0 "$DUCK_PID" 2>/dev/null; then
        if take_duckstation_screenshot SCREENSHOT_FILE2; then
            echo "Screenshot ${capture_index} saved to: $SCREENSHOT_FILE2"

            if [ -x "$PWD/scripts/decode-ps1-bars.py" ]; then
                echo ""
                echo "=== Telemetry decode (capture ${capture_index}/3) ==="
                "$PWD/scripts/decode-ps1-bars.py" --include-zero "$SCREENSHOT_FILE2" || true
            fi
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
