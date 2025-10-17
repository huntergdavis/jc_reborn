#!/bin/bash
#
# Capture reference frames for visual regression testing
# These frames represent the "known good" state of rendering
#

set -e

cd "$(dirname "$0")/../jc_resources"

mkdir -p ../tests/visual_reference

echo "Capturing reference frames..."
echo ""

# Capture frame 10 from GJNAT1.TTM (simple scene)
echo "1. Capturing GJNAT1.TTM frame 10..."
timeout 8 ../jc_reborn window nosound capture-frame 10 capture-output ../tests/visual_reference/gjnat1_frame10.bmp ttm GJNAT1.TTM &
sleep 5
pkill -9 jc_reborn 2>/dev/null || true
wait 2>/dev/null || true

# Capture frame 20 from GJNAT1.TTM
echo "2. Capturing GJNAT1.TTM frame 20..."
timeout 10 ../jc_reborn window nosound capture-frame 20 capture-output ../tests/visual_reference/gjnat1_frame20.bmp ttm GJNAT1.TTM &
sleep 7
pkill -9 jc_reborn 2>/dev/null || true
wait 2>/dev/null || true

# Capture frame 15 from GJVIS3.TTM
echo "3. Capturing GJVIS3.TTM frame 15..."
timeout 10 ../jc_reborn window nosound capture-frame 15 capture-output ../tests/visual_reference/gjvis3_frame15.bmp ttm GJVIS3.TTM &
sleep 7
pkill -9 jc_reborn 2>/dev/null || true
wait 2>/dev/null || true

echo ""
echo "Reference frames captured:"
ls -lh ../tests/visual_reference/*.bmp 2>/dev/null || echo "No frames captured"

echo ""
echo "These reference frames represent the current (known-good) rendering state."
echo "Future builds will be compared against these frames to detect visual regressions."
