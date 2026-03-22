#!/bin/bash
# Build the DuckStation regtest Docker image.
#
# This is a one-time operation.  After the first build (~15-30 min),
# Docker's layer cache makes rebuilds near-instant unless the Dockerfile
# or DuckStation source changes.
#
# Usage:
#   ./scripts/build-regtest-image.sh              # normal build
#   ./scripts/build-regtest-image.sh --no-cache    # force full rebuild
#
# The resulting image is tagged jc-reborn-regtest:latest and contains
# only the duckstation-regtest binary plus its runtime dependencies.

set -euo pipefail

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

cd "$(dirname "$0")/.."  # project root

DOCKERFILE="config/ps1/Dockerfile.regtest"
IMAGE_TAG="jc-reborn-regtest:latest"

if [ ! -f "$DOCKERFILE" ]; then
    echo "ERROR: $DOCKERFILE not found." >&2
    exit 1
fi

# Parse arguments
DOCKER_BUILD_FLAGS=()
for arg in "$@"; do
    case "$arg" in
        --no-cache)
            DOCKER_BUILD_FLAGS+=("--no-cache")
            ;;
        --help|-h)
            echo "Usage: $0 [--no-cache] [--help]"
            echo ""
            echo "Build the DuckStation regtest Docker image for headless PS1 testing."
            echo ""
            echo "Options:"
            echo "  --no-cache   Force a full rebuild (ignore Docker layer cache)"
            echo "  --help       Show this help message"
            echo ""
            echo "The image is tagged as: $IMAGE_TAG"
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg" >&2
            echo "Run '$0 --help' for usage." >&2
            exit 1
            ;;
    esac
done

echo "======================================"
echo "Building DuckStation Regtest Image"
echo "======================================"
echo ""
echo "Dockerfile: $DOCKERFILE"
echo "Image tag:  $IMAGE_TAG"
echo ""
echo "This compiles DuckStation from source."
echo "First build may take 15-30 minutes."
echo ""

BUILD_START=$(date +%s)

docker build \
    --platform linux/amd64 \
    ${DOCKER_BUILD_FLAGS[@]+"${DOCKER_BUILD_FLAGS[@]}"} \
    -f "$DOCKERFILE" \
    -t "$IMAGE_TAG" \
    .

BUILD_END=$(date +%s)
BUILD_DURATION=$(( BUILD_END - BUILD_START ))
BUILD_MINUTES=$(( BUILD_DURATION / 60 ))
BUILD_SECONDS=$(( BUILD_DURATION % 60 ))

echo ""
echo "======================================"
echo "Build Complete"
echo "======================================"
echo ""
echo "Image:      $IMAGE_TAG"
echo "Build time: ${BUILD_MINUTES}m ${BUILD_SECONDS}s"
echo "Image size: $(docker images "$IMAGE_TAG" --format '{{.Size}}')"
echo ""

# Quick smoke test: print the binary's help output.
echo "Smoke test (--help):"
docker run --rm --platform linux/amd64 "$IMAGE_TAG" -help 2>&1 | head -5 || true

echo ""
echo "Next steps:"
echo "  ./scripts/run-regtest.sh --cue path/to/game.cue --bios path/to/bios/"
echo ""
