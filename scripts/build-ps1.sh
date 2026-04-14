#!/bin/bash
# PS1 Build Script - Builds jcreborn.exe using Docker and PSn00bSDK
# Usage: ./build-ps1.sh [clean]

set -e  # Exit on error

cd "$(dirname "$0")/.."  # Change to project root
# shellcheck source=./docker-common.sh
source "scripts/docker-common.sh"
docker_init

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

if [ "${1:-}" = "clean" ]; then
    echo "=== Cleaning build directory ==="
    "${DOCKER_CMD[@]}" run --rm --platform linux/amd64 \
        -v "$PWD":/project \
        jc-reborn-ps1-dev:amd64 \
        bash -lc '
            set -e
            rm -rf /project/build-ps1/*
            cmake -S /project -B /project/build-ps1
        '
fi

echo "=== Building PS1 executable ==="
"${DOCKER_CMD[@]}" run --rm --platform linux/amd64 \
    -v "$PWD":/project \
    jc-reborn-ps1-dev:amd64 \
    bash -lc '
        set -e
        cmake -S /project -B /project/build-ps1
        cd /project/build-ps1
        if ! make jcreborn; then
            echo "=== Falling back to manual PS1 link ==="
            rm -f jcreborn.elf jcreborn.map
            LINK_CMD="$(cat CMakeFiles/jcreborn.dir/link.txt)"
            bash -lc "$LINK_CMD"
            /opt/psn00bsdk/PSn00bSDK-0.24-Linux/bin/elf2x -q /project/build-ps1/jcreborn.elf /project/build-ps1/jcreborn.exe
            /opt/psn00bsdk/PSn00bSDK-0.24-Linux/bin/mipsel-none-elf-nm -f posix -l -n /project/build-ps1/jcreborn.elf >/project/build-ps1/jcreborn.map
        fi
    '

echo ""
echo "=== Build complete ==="
ls -lh build-ps1/jcreborn.exe

exit 0
