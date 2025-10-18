# Johnny Reborn - PlayStation 1 Port

This directory contains the PS1 port of Johnny Reborn using PSn00bSDK.

## Quick Start

### Prerequisites
- Docker Desktop installed ([Download](https://www.docker.com/products/docker-desktop))
- DuckStation PS1 emulator (already downloaded to `/tmp/DuckStation.app`)

### Building

```bash
# Run the automated build script
./build-ps1.sh
```

This will:
1. Build the Docker development environment (first run only)
2. Compile Johnny Reborn for PS1
3. Generate CD image (jcreborn.bin/jcreborn.cue)

### Testing

1. Open DuckStation: `/tmp/DuckStation.app`
2. File → Boot Disc Image
3. Select `jcreborn.bin`

## Project Status

**Current Branch:** `ps1`
**Base Branch:** `4mb2025` (350KB memory usage)

### ✅ Completed

- [x] Research PSn00bSDK and PS1 development tools
- [x] Download DuckStation emulator
- [x] Create comprehensive porting documentation
- [x] Set up Docker development environment
- [x] Create build automation scripts

### 🚧 In Progress

- [ ] Create Makefile.ps1
- [ ] Port graphics layer (graphics.c → graphics_ps1.c)
- [ ] Port audio layer (sound.c → sound_ps1.c)
- [ ] Port input layer (events.c → events_ps1.c)
- [ ] Adapt resource I/O for CD-ROM
- [ ] Test in emulator

## Documentation

- **PS1_PORT_PLAN.md** - Comprehensive technical port plan
- **PS1_SETUP_NOTES.md** - Development environment setup notes
- **PS1_TOOLCHAIN_STATUS.md** - Toolchain installation status and approach

## Technical Overview

### Why PS1?

Johnny Reborn's optimized memory usage (350KB) and native 640x480 resolution make it a perfect fit for PS1:
- **PS1 System RAM**: 2MB (plenty of headroom)
- **PS1 VRAM**: 1MB
- **PS1 Native Resolution**: 640x480 ✓

### Architecture

The port strategy keeps most of the codebase unchanged:

**No changes needed:**
- Core engine (ttm.c, ads.c, story.c)
- Resource system (resource.c, uncompress.c)
- Game logic (walk.c, calcpath.c, island.c)
- Utilities (utils.c, config.c, bench.c)

**Need PS1-specific implementations:**
- `graphics_ps1.c` - SDL2 → PSn00bSDK GPU
- `sound_ps1.c` - SDL_mixer → PSn00bSDK SPU
- `events_ps1.c` - SDL events → PSX controller
- `resource.c` - stdio → PSn00bSDK CD-ROM I/O

### Memory Layout

- **Code + Data**: ~200KB
- **Resource Cache**: 1.5MB (LRU with pinning)
- **VRAM**: 600KB framebuffers + texture cache
- **SPU RAM**: 512KB for sound effects

## Building Manually

If you prefer to build without Docker:

```bash
# Install PSn00bSDK (Linux only)
# Download from: https://github.com/Lameguy64/PSn00bSDK/releases

# Build
export PATH="/opt/psn00bsdk/bin:$PATH"
make -f Makefile.ps1

# Create CD image
mkpsxiso cd_layout.xml
```

## Files

```
PS1_PORT_PLAN.md         - Technical porting strategy
PS1_SETUP_NOTES.md       - Setup instructions
PS1_TOOLCHAIN_STATUS.md  - Toolchain installation notes
PS1_README.md            - This file

Dockerfile.ps1           - Docker build environment
build-ps1.sh            - Automated build script

Makefile.ps1            - PS1-specific makefile (TODO)
cd_layout.xml           - CD image layout (TODO)

graphics_ps1.c/h        - PS1 graphics layer (TODO)
sound_ps1.c/h           - PS1 audio layer (TODO)
events_ps1.c/h          - PS1 input layer (TODO)
```

## References

- [PSn00bSDK](https://github.com/Lameguy64/PSn00bSDK) - PS1 SDK
- [mkpsxiso](https://github.com/Lameguy64/mkpsxiso) - CD image tool
- [DuckStation](https://github.com/stenzek/duckstation) - PS1 emulator
- [PS1 Dev Resources](https://psx.arthus.net/) - Documentation

## Controller Mapping

| PSX Button | Action |
|-----------|--------|
| Start | Pause/Unpause |
| Select | Toggle debug |
| Triangle | Advance frame (paused) |
| Circle | Toggle max speed |
| X/□/L1/L2/R1/R2 | Reserved |

## License

Same as main project (GPL-3.0). This port uses PSn00bSDK which is also open source.
