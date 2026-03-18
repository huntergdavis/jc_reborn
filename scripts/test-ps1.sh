#!/bin/bash
# Launch DuckStation - Runs the PS1 emulator with jcreborn.cue
# Usage: ./test-ps1.sh

set -e  # Exit on error

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

cd "$(dirname "$0")/.."  # Change to project root

if [ ! -f "jcreborn.cue" ]; then
    echo "ERROR: jcreborn.cue not found. Run ./scripts/make-cd-image.sh first."
    exit 1
fi

echo "=== Launching DuckStation with jcreborn.cue ==="
# Grant filesystem access to workspace directory
flatpak run --filesystem="$PWD" org.duckstation.DuckStation "$PWD/jcreborn.cue" &

echo "DuckStation launched in background"
exit 0
