# Johnny Reborn - PlayStation 1 Port

Quick start guide for building and testing the PS1 port.

## Quick Start

### Prerequisites
- Docker Desktop installed ([Download](https://www.docker.com/products/docker-desktop))
- DuckStation PS1 emulator

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

1. Open DuckStation emulator
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
- [x] Implement graphics layer
- [x] Implement input layer
- [x] Implement audio layer (skeleton)

### 🚧 In Progress

- [ ] Complete sprite rendering
- [ ] CD-ROM resource loading
- [ ] Audio playback implementation
- [ ] Full integration testing

## Why PS1?

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

**PS1-specific implementations:**
- `graphics_ps1.c` - SDL2 → PSn00bSDK GPU
- `sound_ps1.c` - SDL_mixer → PSn00bSDK SPU
- `events_ps1.c` - SDL events → PSX controller
- `cdrom_ps1.c` - CD-ROM file I/O

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

## Controller Mapping

| PSX Button | Action |
|-----------|--------|
| Start | Pause/Unpause |
| Select | Toggle debug |
| Triangle | Advance frame (paused) |
| Circle | Toggle max speed |
| X/□/L1/L2/R1/R2 | Reserved |

## Documentation

- **[Hardware Specs](hardware-specs.md)** - PS1 technical specifications
- **[API Mapping](api-mapping.md)** - SDL2 → PSn00bSDK translation
- **[Build System](build-system.md)** - CMake, Docker, CD generation
- **[Graphics Layer](graphics-layer.md)** - GPU implementation details
- **[Audio Layer](audio-layer.md)** - SPU implementation details
- **[Input Layer](input-layer.md)** - Controller implementation details
- **[Current Status](current-status.md)** - Progress metrics
- **[Toolchain Setup](toolchain-setup.md)** - Development environment
- **[Development Workflow](development-workflow.md)** - Build and test procedures
- **[Project History](project-history.md)** - Journey and lessons learned

## References

- [PSn00bSDK](https://github.com/Lameguy64/PSn00bSDK) - PS1 SDK
- [mkpsxiso](https://github.com/Lameguy64/mkpsxiso) - CD image tool
- [DuckStation](https://github.com/stenzek/duckstation) - PS1 emulator
- [PS1 Dev Resources](https://psx.arthus.net/) - Documentation

## License

Same as main project (GPL-3.0). This port uses PSn00bSDK which is also open source.
