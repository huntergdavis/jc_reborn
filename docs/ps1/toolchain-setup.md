# PS1 Toolchain Setup

Guide to setting up the PlayStation 1 development environment for Johnny Reborn.

## Overview

The PS1 port uses PSn00bSDK 0.24 and runs in a Docker container for reproducible builds across platforms (Linux, macOS, Windows).

## Prerequisites

- **Docker Desktop**: [Download here](https://www.docker.com/products/docker-present)
- **Git**: For cloning the repository
- **DuckStation Emulator**: For testing PS1 builds

## Quick Setup (Recommended)

The easiest way to build for PS1 is using the provided Docker environment:

```bash
# 1. Clone the repository
git clone https://github.com/yourusername/jc_reborn.git
cd jc_reborn
git checkout ps1

# 2. Build Docker image (first time only, ~5 minutes)
docker build -f Dockerfile.ps1 -t jc-reborn-ps1-dev:amd64 --platform linux/amd64 .

# 3. Build PS1 executable
./build-ps1.sh

# 4. Test in DuckStation
# Load jcreborn.cue in DuckStation emulator
```

## Docker Environment

### Dockerfile.ps1 Contents

The Docker image includes:
- **Base**: Ubuntu 22.04 (linux/amd64)
- **PSn00bSDK 0.24**: PS1 SDK with full GPU/SPU/CD support
- **mipsel-none-elf-gcc 12.3.0**: MIPS cross-compiler
- **mkpsxiso**: CD image creation tool
- **elf2x**: ELF to PS-EXE converter
- **CMake 3.22+**: Build system

### Container Contents

```
/opt/psn00bsdk/          PSn00bSDK installation
/usr/bin/mipsel-*        MIPS toolchain binaries
/project/                Mounted source directory
```

### Platform Note

The Docker image is built for `linux/amd64` platform. This works on:
- **Intel Macs**: Native execution
- **Apple Silicon Macs**: Rosetta 2 emulation (slight performance cost)
- **Linux x86-64**: Native execution
- **Windows**: WSL2 + Docker Desktop

## Manual Toolchain Installation (Advanced)

If you prefer to install the toolchain natively (Linux only):

### 1. Install Dependencies

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install build-essential cmake git wget
```

### 2. Download PSn00bSDK

```bash
wget https://github.com/Lameguy64/PSn00bSDK/releases/download/v0.24/psn00bsdk-0.24-linux.tar.gz
sudo tar -xzf psn00bsdk-0.24-linux.tar.gz -C /opt/
```

### 3. Install MIPS Toolchain

```bash
wget https://github.com/Lameguy64/PSn00bSDK/releases/download/v0.24/mipsel-none-elf-gcc-12.3.0-linux.tar.gz
sudo tar -xzf mipsel-none-elf-gcc-12.3.0-linux.tar.gz -C /opt/
```

### 4. Set PATH

```bash
export PATH="/opt/psn00bsdk/bin:/opt/mipsel-none-elf/bin:$PATH"
echo 'export PATH="/opt/psn00bsdk/bin:/opt/mipsel-none-elf/bin:$PATH"' >> ~/.bashrc
```

### 5. Build

```bash
mkdir build-ps1 && cd build-ps1
cmake -DCMAKE_TOOLCHAIN_FILE=/opt/psn00bsdk/lib/libpsn00b/cmake/toolchain.cmake ..
make
mkpsxiso ../cd_layout.xml
```

## macOS-Specific Notes

### Precompiled Toolchain Attempt (Unsuccessful)

We initially tried the precompiled macOS toolchain from https://psx.arthus.net/sdk/mipsel/:
- Downloaded `mipsel-none-elf-binutils-2.37-macos.zip` (11.5 MB)
- Downloaded `mipsel-none-elf-gcc-11.2-macos.zip` (38.9 MB)
- Installed to `~/ps1-toolchain/`

**Result**: Partial success
- GCC version check works: `mipsel-none-elf-gcc (GCC) 11.2.0`
- Missing internal components (cc1, cc1plus)
- Incompatible with macOS 14.5.0 (built for macOS 10.15)

### Why Docker on macOS

- **Reproducibility**: Same environment on all platforms
- **No dependency hell**: Everything pre-configured
- **Easy updates**: Rebuild Docker image when PSn00bSDK updates
- **Works on M1/M2 Macs**: Via Rosetta 2 emulation

## DuckStation Emulator Setup

### macOS Installation

1. Download from [DuckStation Releases](https://github.com/stenzek/duckstation/releases)
2. Extract to `/tmp/DuckStation.app` (or wherever you prefer)
3. Open DuckStation
4. Configure:
   - **BIOS**: Point to PS1 BIOS file (not included, must obtain separately)
   - **Settings → Console**: Set region to NTSC-U or NTSC-J
   - **Settings → Display**: 640x480 resolution

### Linux Installation

```bash
# AppImage
wget https://github.com/stenzek/duckstation/releases/latest/download/duckstation-qt-x64.AppImage
chmod +x duckstation-qt-x64.AppImage
./duckstation-qt-x64.AppImage

# Or use Flatpak
flatpak install flathub org.duckstation.DuckStation
flatpak run org.duckstation.DuckStation
```

### Testing Your Build

1. Launch DuckStation
2. **File → Start Disc**
3. Select `jcreborn.cue` (not .bin!)
4. Watch for colored screens (visual debug indicators)
5. Check DuckStation TTY console for debug output (if enabled)

## Troubleshooting

### Docker Build Fails

**Error**: "Cannot connect to Docker daemon"
```bash
# Start Docker Desktop and ensure it's running
docker info  # Should show system information
```

**Error**: "Platform not supported"
```bash
# Make sure to specify --platform linux/amd64
docker build --platform linux/amd64 -f Dockerfile.ps1 -t jc-reborn-ps1-dev:amd64 .
```

### Permission Issues

**Error**: "Permission denied" when accessing build outputs
```bash
# Fix ownership (don't use sudo for Docker commands!)
sudo chown -R $USER:$USER .
```

**CRITICAL**: Never use `sudo` with Docker commands - it breaks permissions and DuckStation access!

### Build Errors

**Missing toolchain binaries**:
- Rebuild Docker image: `docker build --no-cache -f Dockerfile.ps1 ...`

**CMake configuration fails**:
- Check CMakeLists.ps1.txt paths
- Verify PSn00bSDK installation in container

### DuckStation Won't Load CD

**Problem**: "Invalid disc image" error
- Make sure you're loading the `.cue` file, not `.bin`
- Verify CD image was created: `ls -lh jcreborn.cue jcreborn.bin`
- Check CD layout in `cd_layout.xml`

**Problem**: Black screen, no output
- Check BIOS configuration in DuckStation
- Enable TTY console output in settings
- Look for visual debug colored screens

## Build Verification

After setting up the toolchain, verify it works:

```bash
# Test Docker environment
docker run --rm jc-reborn-ps1-dev:amd64 mipsel-none-elf-gcc --version
# Should output: mipsel-none-elf-gcc (GCC) 12.3.0

# Test full build
./build-ps1.sh
# Should create: jcreborn.bin and jcreborn.cue

# Check file sizes
ls -lh jcreborn.bin
# Should be ~650KB (CD image size)
```

## Resources

### Official Documentation
- [PSn00bSDK GitHub](https://github.com/Lameguy64/PSn00bSDK)
- [PSn00bSDK Library Reference (PDF)](https://psx.arthus.net/sdk/PSn00SDK/Docs/libn00bref.pdf)
- [mkpsxiso Documentation](https://github.com/Lameguy64/mkpsxiso)
- [DuckStation Wiki](https://github.com/stenzek/duckstation/wiki)

### Community Resources
- [PS1 Dev Resources](https://psx.arthus.net/)
- [PlayStation Development Network](https://www.psxdev.net/)
- [Lameguy's Tutorials](http://rsync.irixnet.org/tutorials/pstutorials/)

### Tools & Downloads
- [PSn00bSDK Releases](https://github.com/Lameguy64/PSn00bSDK/releases)
- [DuckStation Downloads](https://github.com/stenzek/duckstation/releases)
- [MIPS Toolchain (macOS - partial)](https://psx.arthus.net/sdk/mipsel/)

## See Also

- [Build System](build-system.md) - CMake and CD image generation
- [Development Workflow](development-workflow.md) - Build and test procedures
- [Current Status](current-status.md) - Progress and next steps
