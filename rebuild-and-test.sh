#!/bin/bash
# Full PS1 Rebuild and Test - Complete workflow from source to emulator
# Usage: ./rebuild-and-test.sh [clean]

set -e  # Exit on error

cd "$(dirname "$0")"  # Change to script directory

echo "======================================"
echo "PS1 Full Rebuild and Test Workflow"
echo "======================================"
echo ""

# Step 1: Build executable
if [ "$1" = "clean" ]; then
    ./build-ps1.sh clean
else
    ./build-ps1.sh
fi

echo ""

# Step 2: Create CD image
./make-cd-image.sh

echo ""

# Step 3: Kill any existing DuckStation instances
echo "=== Cleaning up old DuckStation instances ==="
pkill -9 -f "duckstation" 2>/dev/null || true
sleep 1

# Step 4: Launch DuckStation
./test-ps1.sh

echo ""
echo "======================================"
echo "Workflow complete!"
echo "DuckStation should be running now."
echo "======================================"

exit 0
