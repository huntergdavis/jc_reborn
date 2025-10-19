# Docker Cross-Compilation Environment Setup Skill

Create Docker-based cross-compilation environments for embedded systems (PS1, Dreamcast, RetroFW, etc.).

## Usage

Run this skill when setting up a new cross-compilation toolchain or porting to a new embedded platform.

## Task

1. **Research SDK/Toolchain**
   - Identify official SDK (e.g., PSn00bSDK, KallistiOS, buildroot)
   - Find latest stable release URLs
   - Check for prebuilt binaries vs. source builds
   - Document minimum requirements (CMake version, dependencies)

2. **Create Dockerfile**
   - Base image selection (Ubuntu 22.04 recommended for stability)
   - Install system dependencies (cmake, make, wget, unzip, etc.)
   - Download and install SDK/toolchain
   - Set environment variables (PATH, SDK paths)
   - Configure working directory

3. **Test Docker Build**
   - Build image with appropriate platform flag
   - For Apple Silicon Macs: `--platform linux/amd64` for x86-64 emulation
   - Verify toolchain installation (gcc --version, etc.)
   - Test simple compilation

4. **Document Usage**
   - Build command
   - Run command with volume mounts
   - Common troubleshooting tips

## Dockerfile Template

```dockerfile
FROM ubuntu:22.04

# Avoid interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
RUN apt-get update && apt-get install -y \
    wget curl unzip make cmake build-essential git \
    && rm -rf /var/lib/apt/lists/*

# Create SDK directory
RUN mkdir -p /opt/sdk

# Download and install SDK (example)
RUN cd /tmp && \
    wget -q https://example.com/sdk-release.zip && \
    unzip -q sdk-release.zip -d /opt/sdk && \
    rm sdk-release.zip

# Set environment variables
ENV PATH="/opt/sdk/bin:${PATH}"
ENV SDK_ROOT="/opt/sdk"

# Set working directory
WORKDIR /project

CMD ["/bin/bash"]
```

## Apple Silicon Mac Considerations

When running on ARM64 Macs:
- Use `--platform linux/amd64` flag for x86-64 binaries
- Docker Desktop provides automatic emulation via QEMU
- Expect ~2x slower compilation vs. native
- Build command: `docker build --platform linux/amd64 -f Dockerfile.target -t project-dev:amd64 .`
- Run command: `docker run --rm --platform linux/amd64 -v $(pwd):/project project-dev:amd64 <cmd>`

## Testing Checklist

- [ ] Docker image builds successfully
- [ ] Toolchain binaries execute (test with `--version`)
- [ ] Environment variables set correctly
- [ ] Can compile simple "hello world" program
- [ ] Volume mounts work correctly
- [ ] Build artifacts persist after container exits

## Common Platforms

### PlayStation 1
- SDK: PSn00bSDK
- Toolchain: mipsel-none-elf-gcc
- Architecture: MIPS R3000A
- Resources: https://github.com/Lameguy64/PSn00bSDK

### Dreamcast
- SDK: KallistiOS
- Toolchain: sh-elf-gcc
- Architecture: SuperH SH-4
- Resources: https://github.com/KallistiOS/KallistiOS

### RetroFW (GCW Zero, RS-97)
- SDK: buildroot-based
- Toolchain: mipsel-linux-gcc
- Architecture: MIPS32 (Ingenic JZ4760)
- Resources: https://github.com/retrofw/retrofw

## Success Criteria

- Docker image builds in <5 minutes
- Toolchain verified working
- Documentation complete
- Example build tested successfully
