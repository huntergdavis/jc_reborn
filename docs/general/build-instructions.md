# Johnny Reborn - Build Instructions

This document explains how to build Johnny Reborn for various platforms.

## Build System

The project uses Make with platform-specific Makefiles:

- **macOS**: `make -f Makefile.osx` or `make` (default Makefile works on macOS)
- **Linux**: `make -f Makefile.linux`
- **Windows/MinGW**: `make -f Makefile.MinGW`

**Clean build**: `make clean` then rebuild with appropriate Makefile.

The build requires SDL2 installed via system package manager or Homebrew. All Makefiles compile C99 with `-Wall -Wpedantic`.

## macOS / Linux (SDL2)

**Standard workflow**:
```bash
# 1. Build the executable
make  # macOS (uses default Makefile)
# OR
make -f Makefile.linux  # Linux

# 2. Copy executable to jc_resources directory
cp jc_reborn jc_resources/

# 3. Run from within jc_resources directory
cd jc_resources
./jc_reborn window
```

**Note**: The executable must be run from the `jc_resources` directory because it needs access to the original Sierra data files (`RESOURCE.MAP` and `RESOURCE.001`) and optional sound files (`sound0.wav` through `sound24.wav`). These files are not included in the repository and must be obtained separately (see README.md for details).

## PlayStation 1 (PS1 Branch)

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
- See PS1 documentation in `docs/ps1/` for complete workflow

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

## Clean Build

Always do a clean build when switching platforms or after significant changes:

```bash
make clean
make -f Makefile.<platform>
```

## Troubleshooting

### SDL2 Not Found

**macOS**:
```bash
brew install sdl2
```

**Linux (Ubuntu/Debian)**:
```bash
sudo apt-get install libsdl2-dev
```

**Linux (Fedora)**:
```bash
sudo dnf install SDL2-devel
```

### Missing Resource Files

The engine requires `RESOURCE.MAP` and `RESOURCE.001` in the `jc_resources/` directory. These must be obtained from the original Johnny Castaway screensaver (not included in this repository).

### Permission Issues (PS1)

Never use `sudo` with PS1 Docker commands. If you encounter permission issues, fix ownership:
```bash
sudo chown -R $USER:$USER .
```
