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

# Get timestamp before launch to identify new screenshots
BEFORE_TIME=$(date +%s)

# Kill any existing DuckStation processes
pkill -f duckstation-qt 2>/dev/null || true
pkill -f DuckStation 2>/dev/null || true
sleep 1

echo "=== Launching DuckStation ==="
# Launch DuckStation in batch mode with fast boot
flatpak run org.duckstation.DuckStation -batch -fastboot "$CUE_FILE" &
DUCK_PID=$!

echo "DuckStation PID: $DUCK_PID"
echo "Waiting ${WAIT_TIME} seconds for test to complete..."

# Wait for test completion
sleep "$WAIT_TIME"

echo "=== Taking screenshot ==="
# Check if DuckStation is still running and send F10
if kill -0 "$DUCK_PID" 2>/dev/null; then
    # Use dotool for universal F10 automation (works on both Wayland and X11)
    if command -v dotool >/dev/null 2>&1; then
        echo "Using dotool for F10 screenshot"
        sg input -c 'echo "key F10" | dotool'
        echo "Sent F10 via dotool"
    elif [ "$XDG_SESSION_TYPE" = "wayland" ]; then
        if command -v ydotool >/dev/null 2>&1; then
            echo "Using ydotool for Wayland F10 screenshot"
            ydotool key F10
        else
            echo "ERROR: No Wayland automation tool available"
        fi
    else
        # X11 fallback
        if command -v xdotool >/dev/null 2>&1; then
            DUCK_WINDOW=$(xdotool search --name "DuckStation" | head -1)
            if [ -n "$DUCK_WINDOW" ]; then
                xdotool windowactivate "$DUCK_WINDOW"
                sleep 0.5
                xdotool key F10
                echo "Sent F10 via xdotool"
            fi
        else
            echo "ERROR: xdotool not available for X11 automation"
        fi
    fi

    # Wait for screenshot to be saved
    sleep 3

    # Kill DuckStation if still running
    kill "$DUCK_PID" 2>/dev/null || true
    sleep 1
    pkill -f duckstation-qt 2>/dev/null || true
else
    echo "DuckStation already exited (batch mode worked)"
fi

echo "=== Finding latest screenshot ==="
# Find the most recent screenshot
LATEST_SCREENSHOT=$(find "$SCREENSHOT_DIR" -name "jcreborn *.png" -newer <(date -d "@$BEFORE_TIME" +%Y-%m-%d) 2>/dev/null | sort | tail -1)

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