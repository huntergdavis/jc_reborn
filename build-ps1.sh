#!/bin/bash
# PS1 Build Script - Builds jcreborn.exe using Docker and PSn00bSDK
# Usage: ./build-ps1.sh [clean]

set -e  # Exit on error

cd "$(dirname "$0")"  # Change to script directory

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
