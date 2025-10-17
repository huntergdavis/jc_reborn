#!/bin/bash
#
# Capture comprehensive reference frames for visual regression testing
# Captures frames from ALL TTMs and ADS scripts for complete coverage
#

set -e

SCRIPT_DIR="$(dirname "$0")"
cd "$SCRIPT_DIR/../jc_resources"

mkdir -p ../tests/visual_reference

echo "================================================"
echo "Comprehensive Visual Regression Frame Capture"
echo "================================================"
echo ""
echo "This will capture reference frames from:"
echo "  - All 41 TTM files (frame 10 each)"
echo "  - All 10 ADS files (tag 0, frame 15 each)"
echo ""
echo "Total: ~51 reference frames"
echo "Estimated time: ~10 minutes"
echo ""

# Counter for progress
TOTAL=51
CURRENT=0

# Function to capture a frame with progress
capture_frame() {
    local TYPE=$1
    local NAME=$2
    local FRAME=$3
    local OUTPUT=$4
    local EXTRA_ARGS=$5

    CURRENT=$((CURRENT + 1))
    echo "[$CURRENT/$TOTAL] Capturing $NAME (frame $FRAME)..."

    # Run with timeout, capture in background, kill after delay
    timeout 15 ../jc_reborn window nosound capture-frame $FRAME capture-output "$OUTPUT" $TYPE "$NAME" $EXTRA_ARGS > /dev/null 2>&1 &
    local PID=$!

    # Wait for capture (give it enough time to reach the frame)
    sleep 8

    # Kill the process
    pkill -9 jc_reborn 2>/dev/null || true
    wait $PID 2>/dev/null || true

    # Check if file was created
    if [ -f "$OUTPUT" ]; then
        local SIZE=$(stat -f%z "$OUTPUT" 2>/dev/null || echo "0")
        echo "    ✓ Captured ($SIZE bytes)"
    else
        echo "    ✗ Failed to capture"
    fi

    sleep 1
}

echo "=== TTM FILES (41 total) ==="
echo ""

# Capture frame 10 from each TTM
for ttm in extracted/ttm/*.TTM; do
    basename=$(basename "$ttm" .TTM)
    output="../tests/visual_reference/${basename}_frame10.bmp"
    capture_frame "ttm" "$basename.TTM" 10 "$output"
done

echo ""
echo "=== ADS FILES (10 total) ==="
echo ""

# Capture frame 15 from each ADS (tag 0)
for ads in extracted/ads/*.ADS; do
    basename=$(basename "$ads" .ADS)
    output="../tests/visual_reference/${basename}_tag0_frame15.bmp"
    capture_frame "ads" "$basename.ADS" 15 "$output" "0"
done

echo ""
echo "================================================"
echo "Capture Complete!"
echo "================================================"
echo ""

# Count successful captures
CAPTURED=$(ls ../tests/visual_reference/*.bmp 2>/dev/null | wc -l | tr -d ' ')
echo "Successfully captured: $CAPTURED/$TOTAL frames"
echo ""

# Show storage used
TOTAL_SIZE=$(du -sh ../tests/visual_reference 2>/dev/null | awk '{print $1}')
echo "Total storage: $TOTAL_SIZE"
echo ""

# List all captured frames
echo "Reference frames:"
ls -lh ../tests/visual_reference/*.bmp 2>/dev/null | awk '{print "  " $9 " (" $5 ")"}'

echo ""
echo "These reference frames represent the current (known-good) rendering state."
echo "Future builds will be compared against these frames to detect visual regressions."
echo ""
echo "To run visual regression tests:"
echo "  cd tests && make test-visual-regression"
