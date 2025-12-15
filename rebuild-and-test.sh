#!/bin/bash
# Full PS1 Rebuild and Test - Complete workflow from source to emulator
# Usage: ./rebuild-and-test.sh [noclean]
# By default, ALWAYS does a clean build to ensure fresh code is tested

set -e  # Exit on error

cd "$(dirname "$0")"  # Change to script directory

echo "======================================"
echo "PS1 Full Rebuild and Test Workflow"
echo "======================================"
echo ""

# Step 1: Build executable (ALWAYS clean unless "noclean" specified)
if [ "$1" = "noclean" ]; then
    echo "=== Incremental build (noclean mode) ==="
    sudo ./build-ps1.sh
else
    echo "=== Clean build (default) ==="
    sudo ./build-ps1.sh clean
fi

echo ""

# Step 2: Create CD image
sudo ./make-cd-image.sh

echo ""

# Step 3: Kill any existing DuckStation instances
echo "=== Cleaning up old DuckStation instances ==="
sudo pkill -9 -f "duckstation" 2>/dev/null || true
sleep 1

# Step 4: Run automated test with screenshot capture
echo "=== Running automated test (30 second wait) ==="
./auto-test-ps1.sh 30

echo ""
echo "======================================"
echo "Workflow complete!"
echo "Check screenshots in:"
echo "$HOME/.var/app/org.duckstation.DuckStation/config/duckstation/screenshots"
echo "======================================"

exit 0
