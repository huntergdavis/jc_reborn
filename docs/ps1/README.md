# Johnny Reborn - PlayStation 1 Port

Quick start guide for building and testing the PS1 port.

## Quick Start

### Prerequisites
- Docker Desktop installed ([Download](https://www.docker.com/products/docker-desktop))
- DuckStation PS1 emulator

### Building

```bash
# Run the automated build script
./scripts/rebuild-and-let-run.sh noclean
```

This will:
1. Build inside the Docker container (`jc-reborn-ps1-dev:amd64`) with PSn00bSDK
2. Compile Johnny Reborn for PS1 via CMake
3. Generate CD image (jcreborn.bin/jcreborn.cue) via mkpsxiso

### Testing

1. Open DuckStation emulator
2. File -> Boot Disc Image
3. Select `jcreborn.bin`

The game boots, loads resources from CD, and cycles through scene animations.

## Project Status

**Current Branch:** `ps1`
**Last Updated:** 2026-03-21

### Completed

- [x] Docker + CMake + mkpsxiso build system (builds routinely)
- [x] Graphics layer (~3300 lines, complete software compositing pipeline)
- [x] CD-ROM I/O (2280 lines, reads from CD image)
- [x] Input layer (controller mapping, pause, debug toggle)
- [x] Resource system (hash-based O(1) lookups, LRU cache with pinning)
- [x] Audio layer skeleton (SPU init, no playback)
- [x] Scene restore pipeline (offline-authored scene contracts)
- [x] Telemetry overlay (5-panel diagnostic system)
- [x] Dirty-rect compositing optimization
- [x] 4-bit indexed sprite format with palette LUTs
- [x] 25/63 scenes verified on DuckStation

### In Progress

- [ ] Expand verified scenes from 25 to 63 (restore contract promotion)
- [ ] Fix ACTIVITY.ADS tag 4 bring-up (stale frame)
- [ ] Unblock BUILDING.ADS and FISHING.ADS entry paths
- [ ] Audio playback implementation (WAV -> VAG, SPU channels)
- [ ] Automated validation harness

## Why PS1?

Johnny Reborn's optimized memory usage (350KB) and native 640x480 resolution make it a perfect fit for PS1:
- **PS1 System RAM**: 2MB (plenty of headroom)
- **PS1 VRAM**: 1MB
- **PS1 Native Resolution**: 640x480

### Architecture

The port keeps most of the codebase unchanged:

**No changes needed:**
- Core engine (ttm.c, ads.c, story.c)
- Game logic (walk.c, calcpath.c, island.c)
- Utilities (utils.c, config.c, bench.c)

**PS1-specific implementations:**
- `graphics_ps1.c` - SDL2 -> PSn00bSDK GPU (software compositing pipeline)
- `sound_ps1.c` - SDL_mixer -> PSn00bSDK SPU (skeleton)
- `events_ps1.c` - SDL events -> PSX controller
- `cdrom_ps1.c` - CD-ROM file I/O (replaces stdio)
- `ps1_restore_pilots.h` - Auto-generated scene restore contracts

**Offline pipeline (scripts/):**
- Scene analyzer -> restore specs -> cluster contracts -> pack compiler -> header generator
- 63 scene specs, 34 cluster contracts, 26 restore pilots active

### Memory Layout

- **Code + Data**: ~120KB (PS-EXE)
- **BSS**: ~57KB
- **Resource Cache**: LRU with pinning
- **VRAM**: 600KB framebuffers + texture cache
- **SPU RAM**: 512KB reserved for sound effects

## Building Manually

If you prefer to build without the wrapper script:

```bash
# Build inside Docker
docker run --rm -v $(pwd):/project jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project/build-ps1 && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j4 jcreborn"

# Generate CD image
mkpsxiso cd_layout.xml
```

## Controller Mapping

| PSX Button | Action |
|-----------|--------|
| Start | Pause/Unpause |
| Select | Toggle debug |
| Triangle | Advance frame (paused) |
| Circle | Toggle max speed |
| X/L1/L2/R1/R2 | Reserved |

## Documentation

- **[Hardware Specs](hardware-specs.md)** - PS1 technical specifications
- **[API Mapping](api-mapping.md)** - SDL2 -> PSn00bSDK translation
- **[Build System](build-system.md)** - CMake, Docker, CD generation
- **[Graphics Layer](graphics-layer.md)** - GPU implementation details
- **[Audio Layer](audio-layer.md)** - SPU implementation details
- **[Input Layer](input-layer.md)** - Controller implementation details
- **[Current Status](current-status.md)** - Progress metrics
- **[Toolchain Setup](toolchain-setup.md)** - Development environment
- **[Development Workflow](development-workflow.md)** - Build and test procedures
- **[Project History](project-history.md)** - Journey and lessons learned
- **[Research Package](research/README.md)** - 2026-03 scene-pack and restore research

## References

- [PSn00bSDK](https://github.com/Lameguy64/PSn00bSDK) - PS1 SDK
- [mkpsxiso](https://github.com/Lameguy64/mkpsxiso) - CD image tool
- [DuckStation](https://github.com/stenzek/duckstation) - PS1 emulator
- [PS1 Dev Resources](https://psx.arthus.net/) - Documentation

## License

Same as main project (GPL-3.0). This port uses PSn00bSDK which is also open source.
