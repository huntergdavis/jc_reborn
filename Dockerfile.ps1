FROM ubuntu:22.04

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install basic dependencies
RUN apt-get update && apt-get install -y \
    wget \
    curl \
    unzip \
    make \
    cmake \
    build-essential \
    git \
    && rm -rf /var/lib/apt/lists/*

# Create directory for PSn00bSDK
RUN mkdir -p /opt/psn00bsdk

# Download and install PSn00bSDK v0.24 prebuilt
RUN cd /tmp && \
    wget -q https://github.com/Lameguy64/PSn00bSDK/releases/download/v0.24/PSn00bSDK-0.24-Linux.zip && \
    wget -q https://github.com/Lameguy64/PSn00bSDK/releases/download/v0.24/gcc-mipsel-none-elf-12.3.0-linux.zip && \
    unzip -q PSn00bSDK-0.24-Linux.zip -d /opt/psn00bsdk && \
    unzip -q gcc-mipsel-none-elf-12.3.0-linux.zip -d /opt/psn00bsdk && \
    rm PSn00bSDK-0.24-Linux.zip gcc-mipsel-none-elf-12.3.0-linux.zip

# Add PSn00bSDK binaries to PATH
ENV PATH="/opt/psn00bsdk/bin:/opt/psn00bsdk/PSn00bSDK-0.24-Linux/bin:${PATH}"

# Set PSn00bSDK environment variable (required by CMake)
ENV PSN00BSDK="/opt/psn00bsdk/PSn00bSDK-0.24-Linux"

# Create project directory
WORKDIR /project

# Default command
CMD ["/bin/bash"]
