# PS1 Build System

Details on CMake configuration, Docker environment, and CD image generation for the PlayStation 1 port.

## Overview

The PS1 build system uses:
- **CMake**: Cross-platform build configuration
- **Docker**: Reproducible Linux build environment
- **PSn00bSDK**: PlayStation 1 SDK and toolchain
- **mkpsxiso**: CD image generation tool

## Docker Environment

### Dockerfile.ps1

```dockerfile
FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    unzip

# Install PSn00bSDK
RUN wget https://github.com/Lameguy64/PSn00bSDK/releases/download/v0.24/psn00bsdk-0.24-linux.tar.gz && \
    tar -xzf psn00bsdk-0.24-linux.tar.gz -C /opt/ && \
    rm psn00bsdk-0.24-linux.tar.gz

# Install MIPS toolchain
RUN wget https://github.com/Lameguy64/PSn00bSDK/releases/download/v0.24/mipsel-none-elf-gcc-12.3.0-linux.tar.gz && \
    tar -xzf mipsel-none-elf-gcc-12.3.0-linux.tar.gz -C /opt/ && \
    rm mipsel-none-elf-gcc-12.3.0-linux.tar.gz

# Set environment
ENV PATH="/opt/psn00bsdk/bin:/opt/mipsel-none-elf/bin:$PATH"
WORKDIR /project
```

### Building the Docker Image

```bash
docker build -f Dockerfile.ps1 -t jc-reborn-ps1-dev:amd64 --platform linux/amd64 .
```

**Platform Note**: Built for `linux/amd64` to work on:
- Intel Macs (native)
- Apple Silicon Macs (Rosetta 2)
- Linux x86-64 (native)
- Windows (WSL2)

## CMake Configuration

### CMakeLists.ps1.txt

```cmake
cmake_minimum_required(VERSION 3.16)

# Project definition
project(jcreborn LANGUAGES C)

# PSn00bSDK toolchain
set(CMAKE_TOOLCHAIN_FILE /opt/psn00bsdk/lib/libpsn00b/cmake/toolchain.cmake)

# Source files
set(CORE_SOURCES
    jc_reborn.c
    resource.c
    uncompress.c
    ttm.c
    ads.c
    story.c
    walk.c
    calcpath.c
    island.c
    utils.c
    config.c
    bench.c
)

set(PS1_SOURCES
    graphics_ps1.c
    sound_ps1.c
    events_ps1.c
    cdrom_ps1.c
    ps1_stubs.c
)

# Executable
add_executable(jcreborn ${CORE_SOURCES} ${PS1_SOURCES})

# Include directories
target_include_directories(jcreborn PRIVATE
    ${CMAKE_SOURCE_DIR}
    /opt/psn00bsdk/include
)

# Link PSn00bSDK libraries
target_link_libraries(jcreborn
    psxgpu    # GPU functions
    psxcd     # CD-ROM functions
    psxspu    # SPU audio functions
    psxapi    # System functions
    psxgte    # Geometry engine
    psxsio    # Serial I/O
    psxpress  # CD compression
)

# Convert ELF to PS-EXE format
add_custom_command(TARGET jcreborn POST_BUILD
    COMMAND elf2x -q jcreborn jcreborn.exe
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Converting ELF to PS-EXE"
)
```

### CMake Options

```bash
# Standard build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=/opt/psn00bsdk/lib/libpsn00b/cmake/toolchain.cmake

# Debug build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug

# Release build with optimization
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
```

## CD Image Generation

### cd_layout.xml

```xml
<?xml version="1.0" encoding="UTF-8"?>
<iso_project image_name="jcreborn.bin" cue_sheet="jcreborn.cue">
  <track type="data">
    <directory_tree>
      <!-- PS1 executable -->
      <file name="JCREBORN.EXE" source="build-ps1/jcreborn.exe"/>

      <!-- Resource files -->
      <file name="RESOURCE.MAP" source="jc_resources/RESOURCE.MAP"/>
      <file name="RESOURCE.001" source="jc_resources/RESOURCE.001"/>

      <!-- Sound files (if converted to VAG) -->
      <!-- <file name="SOUND0.VAG" source="sounds/sound0.vag"/> -->
      <!-- ... more sound files ... -->
    </directory_tree>
  </track>
</iso_project>
```

### SYSTEM.CNF

Boot configuration file:

```
BOOT = cdrom:\JCREBORN.EXE;1
TCB = 4
EVENT = 10
STACK = 801FFF00
```

### Creating CD Image

```bash
# Using mkpsxiso
mkpsxiso cd_layout.xml

# Outputs:
# - jcreborn.bin (CD image data)
# - jcreborn.cue (cue sheet with track info)
```

## Build Process

### Manual Build Steps

```bash
# 1. Configure CMake
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project/build-ps1 && \
           cmake -DCMAKE_TOOLCHAIN_FILE=/opt/psn00bsdk/lib/libpsn00b/cmake/toolchain.cmake .."

# 2. Compile
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project/build-ps1 && make"

# 3. Generate CD image
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project && mkpsxiso cd_layout.xml"
```

### Automated Build Script

The `build-ps1.sh` script automates the entire process:

```bash
#!/bin/bash
set -e

echo "=== PS1 Build Script ==="
echo

# Clean build
echo "1. Cleaning..."
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project && rm -rf build-ps1"

# Configure and build
echo "2. Building..."
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project && mkdir build-ps1 && cd build-ps1 && \
           cmake -DCMAKE_TOOLCHAIN_FILE=/opt/psn00bsdk/lib/libpsn00b/cmake/toolchain.cmake .. && \
           make"

# Create CD image
echo "3. Creating CD image..."
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project && mkpsxiso cd_layout.xml"

# Verify outputs
echo "4. Checking outputs..."
ls -lh jcreborn.bin jcreborn.cue build-ps1/jcreborn.elf

echo
echo "=== Build Complete ==="
```

## Build Outputs

### Intermediate Files
```
build-ps1/
├── CMakeFiles/          CMake generated files
├── jcreborn.elf         MIPS ELF executable
├── jcreborn.exe         PS-EXE format (for CD)
├── *.o                  Object files
└── CMakeCache.txt       CMake configuration cache
```

### Final Outputs
```
jcreborn.bin             CD image (BIN format)
jcreborn.cue             Cue sheet (track layout)
```

## Library Linking

### PSn00bSDK Libraries

| Library | Purpose |
|---------|---------|
| `psxgpu` | GPU rendering functions |
| `psxcd` | CD-ROM access |
| `psxspu` | SPU audio functions |
| `psxapi` | System API functions |
| `psxgte` | Geometry transformation engine |
| `psxsio` | Serial I/O |
| `psxpress` | CD compression/decompression |

### Link Order

Link order matters for static libraries:
```cmake
target_link_libraries(jcreborn
    psxgpu    # First - depends on nothing
    psxcd     # May depend on psxapi
    psxspu    # May depend on psxapi
    psxapi    # Depends on psxgpu
    psxgte    # GPU extension
    psxsio    # Serial I/O
    psxpress  # Last - utility library
)
```

## Compiler Flags

### Default Flags (from PSn00bSDK)
```
-msoft-float           Use software floating point
-G0                    No small data optimization
-march=mips1           Target MIPS I architecture
-mabi=32               32-bit ABI
-ffreestanding         Freestanding environment
```

### Custom Flags (in CMakeLists.txt)
```cmake
# Optimization
target_compile_options(jcreborn PRIVATE -O2)

# Warnings
target_compile_options(jcreborn PRIVATE -Wall -Wpedantic)

# Debug info (for debugging builds)
# target_compile_options(jcreborn PRIVATE -g)
```

## Troubleshooting

### CMake Configuration Fails

**Error**: "Could not find toolchain file"
```bash
# Fix: Verify path exists in container
docker run --rm jc-reborn-ps1-dev:amd64 \
  ls /opt/psn00bsdk/lib/libpsn00b/cmake/toolchain.cmake
```

### Linker Errors

**Error**: "undefined reference to 'SpuInit'"
```cmake
# Fix: Add psxspu library
target_link_libraries(jcreborn ... psxspu ...)
```

### CD Image Generation Fails

**Error**: "File not found: RESOURCE.MAP"
```bash
# Fix: Verify resource files exist
ls -lh jc_resources/RESOURCE.MAP jc_resources/RESOURCE.001
```

### Permission Issues

**Error**: "Permission denied" on build outputs
```bash
# Fix: Never use sudo with Docker!
# If files have wrong ownership:
sudo chown -R $USER:$USER build-ps1/ jcreborn.bin jcreborn.cue
```

## See Also

- [Toolchain Setup](toolchain-setup.md) - Installing PSn00bSDK and Docker
- [Development Workflow](development-workflow.md) - Build and test procedures
- [Hardware Specs](hardware-specs.md) - PS1 technical specifications
