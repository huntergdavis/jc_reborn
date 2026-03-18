#!/bin/bash
# PS1 Build Script - Builds jcreborn.exe using Docker and PSn00bSDK
# Usage: ./build-ps1.sh [clean]

set -e  # Exit on error

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

cd "$(dirname "$0")/.."  # Change to project root

if [ "$1" = "clean" ]; then
    echo "=== Cleaning build directory ==="
    docker run --rm --platform linux/amd64 \
        -v "$PWD":/project \
        jc-reborn-ps1-dev:amd64 \
        bash -c "cd /project/build-ps1 && make clean"
fi

echo "=== Building PS1 executable ==="
docker run --rm --platform linux/amd64 \
    -v "$PWD":/project \
    jc-reborn-ps1-dev:amd64 \
    bash -c "cd /project/build-ps1 && make jcreborn"

echo ""
echo "=== Build complete ==="
ls -lh build-ps1/jcreborn.exe

exit 0
