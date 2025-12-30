#!/bin/bash
# Full PS1 Rebuild and Let Run - Builds and launches, keeps running until manual kill
# Usage: ./rebuild-and-let-run.sh [noclean]

set -e  # Exit on error

cd "$(dirname "$0")/.."  # Change to project root

echo "======================================"
echo "PS1 Full Rebuild and Let Run"
echo "======================================"
echo ""

# Step 1: Build executable (ALWAYS clean unless "noclean" specified)
if [ "$1" = "noclean" ]; then
    echo "=== Incremental build (noclean mode) ==="
    sudo ./scripts/build-ps1.sh
else
    echo "=== Clean build (default) ==="
    sudo ./scripts/build-ps1.sh clean
fi

echo ""

# Step 2: Create CD image
sudo ./scripts/make-cd-image.sh

echo ""

# Step 3: Kill any existing DuckStation instances
echo "=== Cleaning up old DuckStation instances ==="
sudo pkill -9 -f "duckstation" 2>/dev/null || true
sleep 1

# Step 4: Launch DuckStation and let it run
echo "=== Launching DuckStation (will keep running) ==="

SCREENSHOT_DIR="$HOME/.var/app/org.duckstation.DuckStation/config/duckstation/screenshots"
CUE_FILE="$PWD/jcreborn.cue"

mkdir -p "$SCREENSHOT_DIR"

# Launch DuckStation with fast boot
flatpak run --filesystem="$(dirname "$CUE_FILE")" org.duckstation.DuckStation -fastboot "$CUE_FILE" &
DUCK_PID=$!

echo "DuckStation PID: $DUCK_PID"
echo "Waiting 30 seconds for initial screenshot..."

sleep 30

# Take first screenshot
if kill -0 "$DUCK_PID" 2>/dev/null; then
    if command -v spectacle >/dev/null 2>&1; then
        SCREENSHOT_FILE="$SCREENSHOT_DIR/ps1-test-$(date +%Y%m%d-%H%M%S).png"
        spectacle -b -n -f -o "$SCREENSHOT_FILE"
        echo "Screenshot 1 saved to: $SCREENSHOT_FILE"
    fi
fi

echo "Waiting 10 more seconds for second screenshot..."
sleep 10

# Take second screenshot
if kill -0 "$DUCK_PID" 2>/dev/null; then
    if command -v spectacle >/dev/null 2>&1; then
        SCREENSHOT_FILE2="$SCREENSHOT_DIR/ps1-test-$(date +%Y%m%d-%H%M%S)-final.png"
        spectacle -b -n -f -o "$SCREENSHOT_FILE2"
        echo "Screenshot 2 saved to: $SCREENSHOT_FILE2"
    fi
fi

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
