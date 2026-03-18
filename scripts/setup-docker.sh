#!/bin/bash
# Setup Docker on Kubuntu for PS1 Development
# This script installs Docker and sets up the PS1 build environment
# Usage: ./setup-docker.sh

set -e  # Exit on error

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

echo "======================================"
echo "PS1 Development - Docker Setup"
echo "======================================"
echo ""

# Check if Docker is already installed
if command -v docker &> /dev/null; then
    echo "✓ Docker is already installed"
    docker --version
    echo ""
else
    echo "Installing Docker..."

    # Update package index
    sudo apt-get update

    # Install prerequisites
    sudo apt-get install -y \
        ca-certificates \
        curl \
        gnupg \
        lsb-release

    # Add Docker's official GPG key
    sudo mkdir -p /etc/apt/keyrings
    curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg

    # Set up the repository
    echo \
      "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu \
      $(lsb_release -cs) stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

    # Install Docker Engine
    sudo apt-get update
    sudo apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

    echo "✓ Docker installed successfully"
fi

# Add current user to docker group
if ! groups $USER | grep -q docker; then
    echo ""
    echo "Adding $USER to docker group..."
    sudo usermod -aG docker $USER
    echo "✓ User added to docker group"
    echo ""
    echo "⚠️  IMPORTANT: You must log out and log back in for group changes to take effect!"
    echo "   After logging back in, run: newgrp docker"
    echo ""
else
    echo "✓ User already in docker group"
fi

# Verify Docker works
echo ""
echo "Testing Docker installation..."
if docker run --rm hello-world &> /dev/null; then
    echo "✓ Docker is working correctly"
else
    echo "⚠️  Docker test failed. You may need to:"
    echo "   1. Log out and log back in"
    echo "   2. Run: newgrp docker"
    echo "   3. Or restart your system"
fi

echo ""
echo "======================================"
echo "Next Steps:"
echo "======================================"
echo "1. If you just installed Docker, log out and log back in"
echo "2. Run: ./scripts/build-docker-image.sh"
echo "3. Run: ./scripts/build-ps1.sh"
echo ""

exit 0
