#!/bin/bash

# Automated PS1 test cycle for Johnny Reborn
# 1. Launch DuckStation with .cue file
# 2. Wait for test to complete
# 3. Take screenshot via F10
# 4. Kill emulator and analyze result

set -e

SCREENSHOT_DIR="$HOME/.var/app/org.duckstation.DuckStation/config/duckstation/screenshots"
CUE_FILE="$PWD/jcreborn.cue"
WAIT_TIME=${1:-13}  # Default 13 seconds, can override

echo "=== Automated PS1 Test Cycle ==="
echo "CUE file: $CUE_FILE"
echo "Wait time: ${WAIT_TIME} seconds"
echo "Screenshot dir: $SCREENSHOT_DIR"

# Ensure screenshot directory exists
mkdir -p "$SCREENSHOT_DIR"

# Create marker file to identify screenshots taken after this point
touch /tmp/.ps1_test_marker_$$

# Kill any existing DuckStation processes
pkill -f duckstation-qt 2>/dev/null || true
pkill -f DuckStation 2>/dev/null || true
sleep 1

echo "=== Launching DuckStation ==="
# Launch DuckStation with fast boot to skip BIOS animation
# Grant filesystem access to workspace directory
flatpak run --filesystem="$(dirname "$CUE_FILE")" org.duckstation.DuckStation -fastboot "$CUE_FILE" &
DUCK_PID=$!

echo "DuckStation PID: $DUCK_PID"
echo "Waiting ${WAIT_TIME} seconds for test to complete..."

# Wait for test completion
sleep "$WAIT_TIME"

echo "=== Taking screenshot ==="
# Check if DuckStation is still running
if kill -0 "$DUCK_PID" 2>/dev/null; then
    # Use spectacle to capture fullscreen (works on KDE Wayland)
    if command -v spectacle >/dev/null 2>&1; then
        echo "Taking screenshot..."
        SCREENSHOT_FILE="$SCREENSHOT_DIR/ps1-test-$(date +%Y%m%d-%H%M%S).png"
        spectacle -b -n -f -o "$SCREENSHOT_FILE"
        echo "Screenshot saved to: $SCREENSHOT_FILE"

        # Wait to let game continue running after screenshot
        echo "Letting game run for 10 more seconds..."
        sleep 10

        # Take second screenshot to see progression
        SCREENSHOT_FILE2="$SCREENSHOT_DIR/ps1-test-$(date +%Y%m%d-%H%M%S)-final.png"
        spectacle -b -n -f -o "$SCREENSHOT_FILE2"
        echo "Final screenshot saved to: $SCREENSHOT_FILE2"
    else
        echo "ERROR: spectacle not available"
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