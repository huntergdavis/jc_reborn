#!/bin/bash
# Analyze PS1 Build Output
# Checks executable sizes, BSS section, and identifies potential boot issues
# Usage: ./analyze-build.sh [path-to-elf]

set -e

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

cd "$(dirname "$0")/.."  # Change to project root

ELF_FILE="${1:-build-ps1/jcreborn.elf}"

echo "======================================"
echo "PS1 Build Analysis"
echo "======================================"
echo ""

if [ ! -f "$ELF_FILE" ]; then
    echo "ERROR: ELF file not found: $ELF_FILE"
    echo ""
    echo "Usage: ./analyze-build.sh [path-to-elf]"
    echo "Example: ./analyze-build.sh build-ps1/jcreborn.elf"
    exit 1
fi

echo "Analyzing: $ELF_FILE"
echo ""

# Check if we need to run in Docker
if ! command -v mipsel-none-elf-size &> /dev/null; then
    echo "Running analysis in Docker container..."
    echo ""
    docker run --rm --platform linux/amd64 \
        -v "$PWD":/project \
        jc-reborn-ps1-dev:amd64 \
        bash -c "cd /project && mipsel-none-elf-size $ELF_FILE && echo '' && mipsel-none-elf-readelf -h $ELF_FILE | grep -E 'Entry point|Class|Machine'"
else
    mipsel-none-elf-size "$ELF_FILE"
    echo ""
    mipsel-none-elf-readelf -h "$ELF_FILE" | grep -E 'Entry point|Class|Machine'
fi

echo ""
echo "======================================"
echo "Analysis Summary"
echo "======================================"
echo ""

# Get the sizes via Docker
SIZE_OUTPUT=$(docker run --rm --platform linux/amd64 \
    -v "$PWD":/project \
    jc-reborn-ps1-dev:amd64 \
    bash -c "cd /project && mipsel-none-elf-size $ELF_FILE" 2>/dev/null | tail -1)

TEXT_SIZE=$(echo "$SIZE_OUTPUT" | awk '{print $1}')
DATA_SIZE=$(echo "$SIZE_OUTPUT" | awk '{print $2}')
BSS_SIZE=$(echo "$SIZE_OUTPUT" | awk '{print $3}')
TOTAL_SIZE=$(echo "$SIZE_OUTPUT" | awk '{print $4}')

echo "Text section: ${TEXT_SIZE} bytes ($(( TEXT_SIZE / 1024 )) KB)"
echo "Data section: ${DATA_SIZE} bytes ($(( DATA_SIZE / 1024 )) KB)"
echo "BSS section:  ${BSS_SIZE} bytes ($(( BSS_SIZE / 1024 )) KB)"
echo "Total:        ${TOTAL_SIZE} bytes ($(( TOTAL_SIZE / 1024 )) KB)"
echo ""

# Check for potential issues
echo "======================================"
echo "Potential Issues"
echo "======================================"
echo ""

ISSUES_FOUND=0

# Check BSS size
if [ "$BSS_SIZE" -gt 51200 ]; then  # 50KB threshold
    echo "⚠️  LARGE BSS: ${BSS_SIZE} bytes ($(( BSS_SIZE / 1024 )) KB)"
    echo "   BSS > 50KB may cause boot issues on PS1"
    echo "   Consider using malloc() instead of static arrays"
    ISSUES_FOUND=1
fi

# Check text size
if [ "$TEXT_SIZE" -gt 102400 ]; then  # 100KB threshold
    echo "⚠️  LARGE TEXT: ${TEXT_SIZE} bytes ($(( TEXT_SIZE / 1024 )) KB)"
    echo "   Text > 100KB may indicate bloated code"
    echo "   Consider optimization or code splitting"
    ISSUES_FOUND=1
fi

# Check total size
if [ "$TOTAL_SIZE" -gt 262144 ]; then  # 256KB threshold
    echo "⚠️  LARGE TOTAL: ${TOTAL_SIZE} bytes ($(( TOTAL_SIZE / 1024 )) KB)"
    echo "   Total > 256KB may cause C runtime init issues"
    ISSUES_FOUND=1
fi

if [ "$ISSUES_FOUND" -eq 0 ]; then
    echo "✓ No obvious size issues detected"
    echo "  BSS: $(( BSS_SIZE / 1024 )) KB (< 50 KB threshold)"
    echo "  Text: $(( TEXT_SIZE / 1024 )) KB (< 100 KB threshold)"
    echo "  Total: $(( TOTAL_SIZE / 1024 )) KB (< 256 KB threshold)"
fi

echo ""

# Check .exe file size
EXE_FILE="${ELF_FILE%.elf}.exe"
if [ -f "$EXE_FILE" ]; then
    EXE_SIZE=$(stat -c%s "$EXE_FILE")
    echo "======================================"
    echo "PS-EXE File"
    echo "======================================"
    echo ""
    echo "File: $EXE_FILE"
    echo "Size: ${EXE_SIZE} bytes ($(( EXE_SIZE / 1024 )) KB)"
    echo ""
fi

echo "======================================"
echo "Comparison to Known Working Builds"
echo "======================================"
echo ""
echo "Minimal test (WORKS):"
echo "  Text: ~4 KB, BSS: ~4 KB, Total: ~8 KB"
echo ""
echo "Target for full game:"
echo "  BSS: < 50 KB (currently: $(( BSS_SIZE / 1024 )) KB)"
echo "  Total: < 256 KB (currently: $(( TOTAL_SIZE / 1024 )) KB)"
echo ""

exit 0
