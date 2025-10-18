#!/bin/bash
# Build script for PS1 port using Docker

set -e

DOCKER_IMAGE="jc-reborn-ps1-dev"

# Color output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Johnny Reborn - PS1 Build Script ===${NC}"

# Check if Docker is installed
if ! command -v docker &> /dev/null; then
    echo -e "${RED}Error: Docker is not installed${NC}"
    echo "Please install Docker Desktop from: https://www.docker.com/products/docker-desktop"
    exit 1
fi

# Build Docker image if it doesn't exist
if [[ "$(docker images -q $DOCKER_IMAGE 2> /dev/null)" == "" ]]; then
    echo -e "${BLUE}Building Docker image...${NC}"
    docker build -f Dockerfile.ps1 -t $DOCKER_IMAGE .
else
    echo -e "${GREEN}Docker image already exists${NC}"
fi

# Run build in container
echo -e "${BLUE}Building PS1 executable...${NC}"
docker run --rm \
    -v "$(pwd):/project" \
    $DOCKER_IMAGE \
    make -f Makefile.ps1

echo -e "${GREEN}Build complete!${NC}"
echo -e "PS1 executable: ${BLUE}./jcreborn.ps-exe${NC}"
echo -e "CD image: ${BLUE}./jcreborn.bin${NC}"
echo -e ""
echo -e "To test in DuckStation:"
echo -e "  1. Open /tmp/DuckStation.app"
echo -e "  2. Load jcreborn.bin"
