#!/bin/bash
# Scene Resource Analyzer
# Analyzes all 63 scenes to report memory usage, sprite counts, thread counts, etc.
# Usage: ./scripts/analyze-scenes.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
ANALYZER="$PROJECT_DIR/scene_analyzer"
RESOURCES_DIR="$PROJECT_DIR/jc_resources"

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

# Build if needed
if [ ! -f "$ANALYZER" ] || [ "$PROJECT_DIR/scene_analyzer.c" -nt "$ANALYZER" ]; then
    echo "Building scene_analyzer..."
    make -C "$PROJECT_DIR" -f Makefile.analyzer
    echo ""
fi

# Check for required resource files
if [ ! -f "$RESOURCES_DIR/RESOURCE.MAP" ]; then
    echo "ERROR: RESOURCE.MAP not found in $RESOURCES_DIR" >&2
    exit 1
fi

if [ ! -d "$RESOURCES_DIR/extracted" ]; then
    echo "ERROR: extracted/ directory not found in $RESOURCES_DIR" >&2
    echo "Run extract_resources first." >&2
    exit 1
fi

# Run analyzer from the resources directory
cd "$RESOURCES_DIR"
exec "$ANALYZER" "$@"
