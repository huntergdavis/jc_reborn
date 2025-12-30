# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Johnny Reborn is an open-source C engine that recreates the classic "Johnny Castaway" screensaver from 1992. It uses SDL2 to play the adventures of Johnny on his desert island by interpreting original Sierra ScreenAntics data files (RESOURCE.MAP and RESOURCE.001).

This is a fork with extensive platform ports including Dreamcast, RetroFW devices, InkPlate displays, embedded systems, and a bash/text-only version. The project includes specialized branches for low-memory systems, closed captions, and various hardware targets.

## Build System

The project uses Make with platform-specific Makefiles:

- **macOS**: `make -f Makefile.osx` or `make` (default Makefile works on macOS)
- **Linux**: `make -f Makefile.linux`
- **Windows/MinGW**: `make -f Makefile.MinGW`

**Clean build**: `make clean` then rebuild with appropriate Makefile.

The build requires SDL2 installed via system package manager or Homebrew. All Makefiles compile C99 with `-Wall -Wpedantic`.

## Build and Run Instructions

### macOS / Linux (SDL2)

**Standard workflow**:
```bash
# 1. Build the executable
make  # macOS (uses default Makefile)
# OR
make -f Makefile.linux  # Linux

# 2. Copy executable to jc_resources directory (which contains RESOURCE.MAP, RESOURCE.001, and sound files)
cp jc_reborn jc_resources/

# 3. Run from within jc_resources directory
cd jc_resources
./jc_reborn window
```

**Note**: The executable must be run from the `jc_resources` directory because it needs access to the original Sierra data files (`RESOURCE.MAP` and `RESOURCE.001`) and optional sound files (`sound0.wav` through `sound24.wav`). These files are not included in the repository and must be obtained separately (see README.md for details).

### PlayStation 1 (PS1 Branch)

**Current branch**: `ps1` (based on `4mb2025` memory-optimized branch)

**CRITICAL: NEVER use sudo with PS1 scripts** - it breaks permissions and DuckStation access. Always run `./rebuild-and-test.sh` without sudo.

**Quick build and test**:
```bash
# Build using Docker container
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project/build-ps1 && make clean && make"

# Create CD image
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project && mkpsxiso cd_layout.xml"

# Test in DuckStation emulator
# Load jcreborn.cue in DuckStation
```

**PS1-specific files**:
- `graphics_ps1.c/h` - PSn00bSDK GPU implementation (640x480 interlaced)
- `sound_ps1.c/h` - SPU audio implementation
- `events_ps1.c/h` - PSX controller input
- `cdrom_ps1.c/h` - CD-ROM file I/O (does NOT call CdInit() - BIOS already initialized)
- `ps1_stubs.c` - Missing libc functions
- `CMakeLists.ps1.txt` - PS1 build configuration

**Important PS1 technical notes**:
- Do NOT call `CdInit()` when booting from CD-ROM (causes crash)
- printf() does not output to DuckStation TTY - use visual debugging (colored screens)
- BSS size reduced from 166KB to 38KB by malloc'ing large buffers
- See `PS1_DEVELOPMENT_GUIDE.md` for detailed PS1 port documentation

## Running the Engine

### Normal Execution

Basic execution: `./jc_reborn` (runs fullscreen story mode)

**Common command patterns**:
- `jc_reborn window` - windowed mode
- `jc_reborn nosound` - disable audio
- `jc_reborn debug` - enable debug output
- `jc_reborn hotkeys` - enable keyboard controls during playback
- `jc_reborn dump` - dump all parsed resources to stdout
- `jc_reborn bench` - run performance benchmark
- `jc_reborn ttm <TTM_NAME>` - play single TTM animation
- `jc_reborn ads <ADS_NAME> <TAG_NO>` - play specific ADS scene with optional `island` flag

Options can be combined: `jc_reborn window nosound debug hotkeys`

**Hotkeys** (when enabled):
- Esc: quit
- Alt+Return: toggle fullscreen
- Space: pause/unpause
- Return: advance one frame when paused
- M: toggle max speed

### Resource Analysis and Extraction

**Extract and analyze resources**:
```bash
# Build resource extraction tools
make -f Makefile.linux  # or Makefile.osx

# Extract all resources from RESOURCE.001
cd jc_resources
./extract_resources

# Analyze resource structure and statistics
./analyze_resources
```

These utilities are helpful for understanding the Sierra ScreenAntics format and debugging resource-related issues.

## Architecture

### Data Pipeline

The engine parses Sierra's proprietary resource format:

1. **Resource Loading** (`resource.c/h`): Parses RESOURCE.MAP index file, locates compressed resources in RESOURCE.001
2. **Decompression** (`uncompress.c/h`): LZ77 decompression for packed data
3. **Resource Types**: ADS (scene scripts), TTM (animation scripts), BMP (sprite sheets), SCR (backgrounds), PAL (palettes)

### Execution Flow

- **Story Mode** (`story.c/h`): Top-level orchestrator that randomly selects scenes and manages Johnny's day-to-day activities
- **ADS Engine** (`ads.c/h`): Scene script interpreter that coordinates multiple TTM animations and manages scene timing/transitions
- **TTM Engine** (`ttm.c/h`): Animation bytecode interpreter with instruction set for sprite control, timing, drawing operations
- **Walk System** (`walk.c/h`, `calcpath.c/h`): Pathfinding for Johnny's transitional walks between scenes

### Rendering

- **Graphics** (`graphics.c/h`): SDL2 wrapper managing screen buffer, sprite blitting, drawing primitives, and palette operations
- **Island Rendering** (`island.c/h`): Procedurally generates the island landmass with random placement and clouds
- **Multi-layer Compositing**: Background layer, TTM animation layers (up to 10 threads), sprite layers (6 BMP slots with 120 sprites each)

### Memory Management

The current branch uses **lazy resource loading with LRU caching** (see MEMORY_OPTIMIZATION_NOTES.md):
- Resources are decompressed on-demand, not at startup
- Active TTM/ADS scripts are pinned in memory to prevent eviction
- BMP/SCR resources are released after converting to SDL surfaces
- Default memory budget is 4MB (configurable via `JC_MEM_BUDGET_MB` environment variable)
- This optimization reduces peak memory from 20MB+ to 2-4MB for typical scenes

**Memory profiling insights** (from `test_memory.c`):
- **Fixed overhead**: ~5KB (resource arrays + calcPath working memory)
- **LZW decompression**: ~16KB working memory during decompress
- **Typical scene**: 400-600KB (1x TTM, 2-3x BMP, 1x SCR, 1x ADS)
- **Peak usage**: 1-2MB with multiple active TTMs
- **Main optimization targets**:
  1. Uncompressed resource data (largest consumer)
  2. Graphics/sprite caching
  3. Multiple TTM slots in memory simultaneously

Run `make test-memory` to see detailed memory analysis.

### Configuration & Events

- **Config** (`config.c/h`): Command-line argument parsing
- **Events** (`events.c/h`): SDL event handling for keyboard/window events
- **Sound** (`sound.c/h`): WAV file playback for sound effects
- **Bench** (`bench.c/h`): Performance testing utilities

## Key Data Structures

- `struct TTtmSlot`: Holds TTM bytecode, sprite sheets, and parsed tag table
- `struct TTtmThread`: Runtime state for executing TTM animation (IP, timers, drawing state)
- `struct TAdsScene`: Scene definition with slot/tag/repeat count
- Resource structs (`TAdsResource`, `TBmpResource`, `TScrResource`, `TTtmResource`): Parsed resource headers with compression metadata

## Branch Structure and Platform Ports

**Main branches**:
- `main` - Primary development (SDL2, desktop platforms)
- `ps1` - PlayStation 1 port (active development, based on 4mb2025)
- `4mb2025` - Memory-optimized version (350KB usage, LRU caching)

**Other platform ports** (separate branches):
- `dreamcast` - Sega Dreamcast port
- `lowmem` / `lowmemdc` - Low-memory embedded systems
- `SDL1.2` / `SDL_1.2_backport` - SDL 1.2 for RetroFW devices
- `inkplate` - InkPlate e-paper displays (20K pre-rendered frames in `rawframes`)
- `bash` - Text-only version
- `closed_captions` - Accessibility features with scene descriptions
- `emscripten` - Web/browser port (incomplete)

**PS1 port specifics** (current branch):
- Built with PSn00bSDK 0.24 and mipsel-none-elf-gcc
- Dockerized build environment for Linux/macOS
- Visual debugging system (colored screens) since printf() doesn't work
- Memory: 350KB peak usage fits easily in PS1's 2MB RAM
- Resolution: Native 640x480 interlaced (matches engine's design)
- Testing: DuckStation emulator recommended
- See `PS1_DEVELOPMENT_GUIDE.md` for complete PS1 workflow

## Testing

The project includes a comprehensive test suite using the Unity testing framework.

**Run all tests**:
```bash
make test
```

**Run specific test suites**:
```bash
cd tests
make test-utils              # Test utility functions
make test-calcpath           # Test pathfinding algorithm
make test-resource           # Test resource loading (requires RESOURCE files)
make test-uncompress         # Test RLE/LZW decompression
make test-config             # Test configuration file I/O
make test-memory             # Memory profiling and analysis
```

**Memory optimization tests**:
```bash
cd tests
make test-disk-streaming     # Disk streaming optimization
make test-bmp-optimization   # BMP data freeing
make test-scr-optimization   # SCR data freeing
make test-lru-cache          # LRU cache with memory budget
```

**Visual regression tests** (requires RESOURCE files):
```bash
cd tests
# Capture reference frames from all scenes
./capture_all_reference_frames.sh

# Run visual regression tests
make test-visual-regression
```

**Test coverage** (46 passing tests):
- `test_utils.c`: 10 tests for utils.c (binary I/O, memory, strings)
- `test_calcpath.c`: 7 tests for calcpath.c (pathfinding algorithm)
- `test_resource.c`: 11 tests for resource.c (parsing, lookup, decompression)
- `test_uncompress.c`: 9 tests for uncompress.c (RLE/LZW decompression)
- `test_config.c`: 11 tests for config.c (configuration file I/O)
- `test_memory.c`: 9 tests for memory profiling and analysis

**Adding new tests**: See `tests/README.md` for detailed instructions.

Tests are designed to:
- Validate core engine logic independent of SDL/graphics
- Run on embedded systems and all platform ports
- Provide regression detection during refactoring
- Execute quickly (< 1 second for full suite)

## Development Workflow

### General Development (main branch)

When modifying the engine:
1. **Run tests before and after changes**: `make test` to catch regressions
2. Changes to resource loading affect `resource.c/h` and require testing with RESOURCE.001
3. TTM instruction changes require understanding the bytecode format (see `ttm.c` opcode handlers)
4. Graphics changes should maintain the 640x480 screen resolution and VGA palette constraints
5. Memory-constrained ports should test against the 4MB budget limit (set via `JC_MEM_BUDGET_MB`)
6. **Add tests for new features**: Unit tests for algorithms, integration tests for workflows

### PS1 Development (ps1 branch)

**Critical PS1-specific rules**:
1. **NEVER call `CdInit()`** - BIOS already initializes CD-ROM; calling it causes crashes
2. **Use visual debugging** - printf() doesn't output to DuckStation TTY console
3. **Minimize BSS** - Use malloc() for large buffers instead of static arrays
4. **Test frequently in DuckStation** - Emulator behavior differs from other platforms
5. **Check build artifacts**: Verify .exe size and BSS size don't exceed ~100KB combined

**PS1 debugging techniques**:
- Colored screen flashes (see graphics_ps1.c:fillScreen())
- RED = main() reached
- GREEN = CD-ROM initialized
- BLUE = Resources parsed
- YELLOW = Error state
- Build minimal test first (`ps1_minimal_main.c`) to verify toolchain

**Common PS1 pitfalls**:
- Large executables may not boot (pre-main crash)
- CD-ROM reads require proper sector alignment
- DuckStation may cache old CD images (delete and recreate .bin/.cue)
- Memory layout differences (heap vs stack)

See `PS1_DEVELOPMENT_GUIDE.md`, `PS1_TESTING_SESSION_*.md` for detailed workflows.
