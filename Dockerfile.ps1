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

# Download and extract PSn00bSDK prebuilt package for Linux
# Note: Replace 'latest' with specific version if needed for reproducibility
RUN cd /tmp && \
    wget -q https://github.com/Lameguy64/PSn00bSDK/releases/download/latest/psn00bsdk-linux.tar.gz && \
    tar -xzf psn00bsdk-linux.tar.gz -C /opt/psn00bsdk --strip-components=1 && \
    rm psn00bsdk-linux.tar.gz

# Add PSn00bSDK binaries to PATH
ENV PATH="/opt/psn00bsdk/bin:${PATH}"

# Set PSn00bSDK environment variables
ENV PSN00BSDK_LIBS="/opt/psn00bsdk/lib"
ENV PSN00BSDK_INCLUDE="/opt/psn00bsdk/include"

# Create project directory
WORKDIR /project

# Default command
CMD ["/bin/bash"]
