#!/bin/bash
# Check PS1 Port Status
# Quick health check of build environment and current build
# Usage: ./check-ps1-status.sh

set -e

cd "$(dirname "$0")/.."  # Change to project root

echo "======================================"
echo "PS1 Port Status Check"
echo "======================================"
echo ""

# 1. Check Docker
echo "=== Docker Status ==="
if command -v docker &> /dev/null; then
    echo "✓ Docker installed: $(docker --version)"

    # Check if user is in docker group
    if groups | grep -q docker; then
        echo "✓ User in docker group"
    else
        echo "⚠️  User NOT in docker group - run: sudo usermod -aG docker $USER"
    fi

    # Check if Docker daemon is running
    if docker ps &> /dev/null; then
        echo "✓ Docker daemon running"
    else
        echo "⚠️  Docker daemon not running - run: sudo systemctl start docker"
    fi
else
    echo "✗ Docker NOT installed - run: ./scripts/setup-docker.sh"
fi
echo ""

# 2. Check Docker image
echo "=== Docker Image Status ==="
if docker images | grep -q "jc-reborn-ps1-dev"; then
    echo "✓ PS1 development image exists"
    docker images | grep "jc-reborn-ps1-dev"
else
    echo "✗ PS1 development image NOT found - run: ./scripts/build-docker-image.sh"
fi
echo ""

# 3. Check DuckStation
echo "=== DuckStation Emulator ==="
if flatpak list | grep -q "org.duckstation.DuckStation"; then
    echo "✓ DuckStation installed via flatpak"
else
    echo "⚠️  DuckStation NOT found - install with:"
    echo "   flatpak install flathub org.duckstation.DuckStation"
fi
echo ""

# 4. Check build artifacts
echo "=== Build Artifacts ==="

if [ -f "build-ps1/jcreborn.elf" ]; then
    echo "✓ jcreborn.elf exists"
    ls -lh build-ps1/jcreborn.elf
else
    echo "✗ jcreborn.elf NOT found - run: ./scripts/build-ps1.sh"
fi

if [ -f "build-ps1/jcreborn.exe" ]; then
    echo "✓ jcreborn.exe exists"
    ls -lh build-ps1/jcreborn.exe
else
    echo "✗ jcreborn.exe NOT found - run: ./scripts/build-ps1.sh"
fi

if [ -f "jcreborn.cue" ] && [ -f "jcreborn.bin" ]; then
    echo "✓ CD image exists"
    ls -lh jcreborn.cue jcreborn.bin
else
    echo "✗ CD image NOT found - run: ./scripts/make-cd-image.sh"
fi
echo ""

# 5. Check resource files
echo "=== Resource Files ==="
if [ -d "jc_resources" ]; then
    echo "✓ jc_resources directory exists"
    if [ -f "jc_resources/RESOURCE.MAP" ] && [ -f "jc_resources/RESOURCE.001" ]; then
        echo "✓ RESOURCE.MAP and RESOURCE.001 found"
        ls -lh jc_resources/RESOURCE.*
    else
        echo "⚠️  RESOURCE files missing - check jc_resources/"
    fi
else
    echo "⚠️  jc_resources directory NOT found"
fi
echo ""

# 6. Run build analysis if executable exists
if [ -f "build-ps1/jcreborn.elf" ] && docker images | grep -q "jc-reborn-ps1-dev"; then
    echo "=== Build Analysis ==="
    ./scripts/analyze-build.sh build-ps1/jcreborn.elf 2>/dev/null || echo "Unable to run analysis"
    echo ""
fi

# 7. Summary
echo "======================================"
echo "Summary"
echo "======================================"
echo ""

READY=true

if ! command -v docker &> /dev/null; then
    echo "⚠️  Install Docker: ./scripts/setup-docker.sh"
    READY=false
fi

if ! docker images | grep -q "jc-reborn-ps1-dev"; then
    echo "⚠️  Build Docker image: ./scripts/build-docker-image.sh"
    READY=false
fi

if [ ! -f "build-ps1/jcreborn.exe" ]; then
    echo "⚠️  Build executable: ./scripts/build-ps1.sh"
    READY=false
fi

if [ ! -f "jcreborn.cue" ]; then
    echo "⚠️  Create CD image: ./scripts/make-cd-image.sh"
    READY=false
fi

if [ "$READY" = true ]; then
    echo "✓ System ready for testing!"
    echo ""
    echo "Next steps:"
    echo "  ./scripts/test-ps1.sh         - Launch in DuckStation"
    echo "  ./scripts/auto-test-ps1.sh    - Automated test with screenshot"
else
    echo ""
    echo "Setup required - follow warnings above"
fi

echo ""

exit 0
