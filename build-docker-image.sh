#!/bin/bash
# Build PS1 Development Docker Image
# Creates the Docker container with PSn00bSDK toolchain
# Usage: ./build-docker-image.sh

set -e  # Exit on error

cd "$(dirname "$0")"

echo "======================================"
echo "Building PS1 Development Docker Image"
echo "======================================"
echo ""

# Check if Dockerfile exists
if [ ! -f "Dockerfile.ps1" ]; then
    echo "ERROR: Dockerfile.ps1 not found!"
    exit 1
fi

echo "Building Docker image: jc-reborn-ps1-dev:amd64"
echo "This may take 5-10 minutes on first build..."
echo ""

# Build with platform specification for compatibility
docker build --platform linux/amd64 -f Dockerfile.ps1 -t jc-reborn-ps1-dev:amd64 .

echo ""
echo "======================================"
echo "Docker Image Built Successfully!"
echo "======================================"
echo ""
echo "Image: jc-reborn-ps1-dev:amd64"
echo ""
echo "Verifying installation..."
docker run --rm --platform linux/amd64 jc-reborn-ps1-dev:amd64 bash -c "mipsel-none-elf-gcc --version | head -1 && mkpsxiso --version 2>&1 | head -1"

echo ""
echo "Next steps:"
echo "  ./build-ps1.sh        - Build the PS1 executable"
echo "  ./make-cd-image.sh    - Create CD image"
echo "  ./test-ps1.sh         - Test in DuckStation"
echo ""

exit 0
