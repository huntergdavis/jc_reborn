#!/bin/bash

# Automated PS1 test cycle for Johnny Reborn
# 1. Launch DuckStation with .cue file
# 2. Wait for test to complete
# 3. Take screenshot via F10
# 4. Kill emulator and analyze result

set -e

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

cd "$(dirname "$0")/.."  # Change to project root

SCREENSHOT_DIR="$HOME/.var/app/org.duckstation.DuckStation/config/duckstation/screenshots"
DUCK_SETTINGS="$HOME/.var/app/org.duckstation.DuckStation/config/duckstation/settings.ini"
CUE_FILE="$PWD/jcreborn.cue"
WAIT_TIME=${1:-35}  # Default waits through title screen and first scene setup
CAPTURE_INTERVAL=${PS1_CAPTURE_INTERVAL:-6}
CAPTURE_COUNT=${PS1_CAPTURE_COUNT:-4}
DUCK_SETTINGS_BACKUP=""
BOOTMODE_FILE="$PWD/config/ps1/BOOTMODE.TXT"
BOOTMODE_BACKUP=""
BOOT_OVERRIDE=""

shift || true
if [ "$#" -gt 0 ]; then
    BOOT_OVERRIDE="$*"
fi

echo "=== Automated PS1 Test Cycle ==="
echo "CUE file: $CUE_FILE"
echo "Wait time: ${WAIT_TIME} seconds"
echo "Capture interval: ${CAPTURE_INTERVAL} seconds"
echo "Capture count: ${CAPTURE_COUNT}"
echo "Screenshot dir: $SCREENSHOT_DIR"

# Ensure screenshot directory exists
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

stage_boot_override() {
    if [ ! -f "$BOOTMODE_FILE" ]; then
        return
    fi

    BOOTMODE_BACKUP="/tmp/ps1-bootmode-$$.txt"
    cp "$BOOTMODE_FILE" "$BOOTMODE_BACKUP"

    if [ -n "$BOOT_OVERRIDE" ]; then
        printf '%s\n' "$BOOT_OVERRIDE" > "$BOOTMODE_FILE"
        echo "Boot override: $BOOT_OVERRIDE"
    else
        : > "$BOOTMODE_FILE"
    fi
}

restore_boot_override() {
    if [ -n "$BOOTMODE_BACKUP" ] && [ -f "$BOOTMODE_BACKUP" ]; then
        cp "$BOOTMODE_BACKUP" "$BOOTMODE_FILE"
        rm -f "$BOOTMODE_BACKUP"
    fi
}

trap 'restore_duckstation_test_settings; restore_boot_override' EXIT

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

decode_screenshot() {
    local label="$1"
    local path="$2"

    if [ -n "$path" ] && [ -x "$PWD/scripts/decode-ps1-bars.py" ]; then
        echo ""
        echo "=== Telemetry decode (${label}) ==="
        "$PWD/scripts/decode-ps1-bars.py" --include-zero "$path" || true
    fi
}

# Create marker file to identify screenshots taken after this point
touch /tmp/.ps1_test_marker_$$

stage_boot_override
if [ -n "$BOOT_OVERRIDE" ]; then
    ./scripts/make-cd-image.sh
fi

# Kill any existing DuckStation processes
pkill -f duckstation-qt 2>/dev/null || true
pkill -f DuckStation 2>/dev/null || true
sleep 1

echo "=== Launching DuckStation ==="
# Launch DuckStation with fast boot to skip BIOS animation
# Grant filesystem access to workspace directory
prepare_duckstation_test_settings
flatpak run --filesystem="$(dirname "$CUE_FILE")" org.duckstation.DuckStation -fastboot "$CUE_FILE" &
DUCK_PID=$!

echo "DuckStation PID: $DUCK_PID"
echo "Waiting ${WAIT_TIME} seconds for test to complete..."

# Wait for test completion
sleep "$WAIT_TIME"

echo "=== Taking screenshot series ==="
# Check if DuckStation is still running
if kill -0 "$DUCK_PID" 2>/dev/null; then
    SCREENSHOT_FILE=""
    LAST_SCREENSHOT=""
    for ((capture_index=1; capture_index<=CAPTURE_COUNT; capture_index++)); do
        if [ "$capture_index" -gt 1 ]; then
            echo "Waiting ${CAPTURE_INTERVAL} more seconds for capture ${capture_index}/${CAPTURE_COUNT}..."
            sleep "$CAPTURE_INTERVAL"
        fi

        if take_duckstation_screenshot CURRENT_SCREENSHOT; then
            echo "Screenshot ${capture_index}/${CAPTURE_COUNT} saved to: $CURRENT_SCREENSHOT"
            if [ -z "$SCREENSHOT_FILE" ]; then
                SCREENSHOT_FILE="$CURRENT_SCREENSHOT"
            fi
            LAST_SCREENSHOT="$CURRENT_SCREENSHOT"
            decode_screenshot "capture ${capture_index}/${CAPTURE_COUNT}" "$CURRENT_SCREENSHOT"
        fi
    done

    SCREENSHOT_FILE2="$LAST_SCREENSHOT"

    if [ -z "$SCREENSHOT_FILE" ]; then
        echo "ERROR: Unable to capture screenshot (xdotool/spectacle unavailable)"
    fi

    # Kill DuckStation if still running
    echo "Closing DuckStation..."
    kill "$DUCK_PID" 2>/dev/null || true
    sleep 1
    pkill -f duckstation-qt 2>/dev/null || true
else
    echo "DuckStation already exited (batch mode worked)"
fi

echo "=== Finding latest screenshot ==="
# Find the most recent screenshot (both DuckStation and spectacle formats)
LATEST_SCREENSHOT=$(find "$SCREENSHOT_DIR" -name "*.png" -newer /tmp/.ps1_test_marker_$$ 2>/dev/null | sort | tail -1)

if [ -n "$LATEST_SCREENSHOT" ]; then
    echo "Latest screenshot: $LATEST_SCREENSHOT"
    echo "Screenshot timestamp: $(stat -c %y "$LATEST_SCREENSHOT")"

    # Return the path for further processing
    echo "SCREENSHOT_PATH=$LATEST_SCREENSHOT"
else
    echo "No new screenshot found!"
    echo "Available screenshots:"
    ls -la "$SCREENSHOT_DIR" | tail -5
    echo "SCREENSHOT_PATH="
fi

echo "=== Test cycle complete ==="
