#!/bin/bash
# Create PS1 CD Image - Uses mkpsxiso to create jcreborn.bin/.cue
# Usage: ./make-cd-image.sh

set -e  # Exit on error

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

cd "$(dirname "$0")/.."  # Change to project root

# Remove old CD image files to prevent mkpsxiso hang
echo "=== Removing old CD image files ==="
rm -f jcreborn.bin jcreborn.cue

echo "=== Creating PS1 CD image with mkpsxiso ==="
docker run --rm --platform linux/amd64 \
    -v "$PWD":/project \
    jc-reborn-ps1-dev:amd64 \
    mkpsxiso -y /project/config/ps1/cd_layout.xml

echo ""
echo "=== CD image created ==="
ls -lh jcreborn.bin jcreborn.cue

exit 0
