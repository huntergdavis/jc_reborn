#!/bin/bash
# Create PS1 CD Image - Uses mkpsxiso to create jcreborn.bin/.cue
# Usage: ./make-cd-image.sh

set -e  # Exit on error

cd "$(dirname "$0")"  # Change to script directory

echo "=== Creating PS1 CD image with mkpsxiso ==="
docker run --rm --platform linux/amd64 \
    -v "$PWD":/project \
    jc-reborn-ps1-dev:amd64 \
    mkpsxiso -y /project/cd_layout.xml

echo ""
echo "=== CD image created ==="
ls -lh jcreborn.bin jcreborn.cue

exit 0
