#!/bin/bash
# Launch DuckStation - Runs the PS1 emulator with jcreborn.cue
# Usage: ./test-ps1.sh

set -e  # Exit on error

cd "$(dirname "$0")"  # Change to script directory

if [ ! -f "jcreborn.cue" ]; then
    echo "ERROR: jcreborn.cue not found. Run ./make-cd-image.sh first."
    exit 1
fi

echo "=== Launching DuckStation with jcreborn.cue ==="
flatpak run org.duckstation.DuckStation "$PWD/jcreborn.cue" &

echo "DuckStation launched in background"
exit 0
