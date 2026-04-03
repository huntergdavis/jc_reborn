#!/bin/bash
# PS1 Build Script - Builds jcreborn.exe using Docker and PSn00bSDK
# Usage: ./build-ps1.sh [clean]

set -e  # Exit on error

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

cd "$(dirname "$0")/.."  # Change to project root

python3 - <<'PY'
import json
from pathlib import Path

root = Path.cwd()
bootmode_path = root / "config/ps1/BOOTMODE.TXT"
header_path = root / "config/ps1/bootmode_embedded.h"
bootmode = ""
if bootmode_path.is_file():
    bootmode = bootmode_path.read_text(encoding="utf-8").strip()

header = (
    "#ifndef PS1_BOOTMODE_EMBEDDED_H\n"
    "#define PS1_BOOTMODE_EMBEDDED_H\n\n"
    f"#define PS1_EMBEDDED_BOOT_OVERRIDE {json.dumps(bootmode)}\n\n"
    "#endif\n"
)
header_path.write_text(header, encoding="utf-8")
PY

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
