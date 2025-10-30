# PS1 Build and Test Workflow

This skill documents the EXACT commands and workflow for building, packaging, and testing the PS1 port of Johnny Reborn.

## CRITICAL: Always Use These Scripts

**DO NOT** run docker, make, mkpsxiso, or duckstation commands manually. Always use the provided scripts.

### Available Scripts

All scripts are in the project root directory:

1. **`./build-ps1.sh`** - Builds the PS1 executable
   - Usage: `./build-ps1.sh` (incremental build)
   - Usage: `./build-ps1.sh clean` (clean rebuild)
   - Output: `build-ps1/jcreborn.exe`

2. **`./make-cd-image.sh`** - Creates the CD image
   - Usage: `./make-cd-image.sh`
   - Output: `jcreborn.bin` and `jcreborn.cue`

3. **`./test-ps1.sh`** - Launches DuckStation emulator
   - Usage: `./test-ps1.sh`
   - Runs DuckStation with `jcreborn.cue`

4. **`./rebuild-and-test.sh`** - Complete workflow
   - Usage: `./rebuild-and-test.sh` (incremental)
   - Usage: `./rebuild-and-test.sh clean` (clean rebuild)
   - Runs: build → create CD → kill old DuckStation → launch DuckStation

## Standard Development Workflow

### Quick Iteration (Code Change → Test)

```bash
# After editing source files:
./rebuild-and-test.sh
```

This does an incremental build and launches the emulator.

### Clean Rebuild

```bash
# If you suspect build artifacts are stale:
./rebuild-and-test.sh clean
```

### Individual Steps (Advanced)

Only use individual scripts if you have a specific reason:

```bash
# Build only (e.g., checking for compilation errors):
./build-ps1.sh

# Rebuild CD only (e.g., after manually editing cd_layout.xml):
./make-cd-image.sh

# Launch emulator only (e.g., CD image already up-to-date):
./test-ps1.sh
```

## Project Structure

```
jc_reborn/
├── build-ps1.sh           # Build script
├── make-cd-image.sh       # CD image creation script
├── test-ps1.sh            # DuckStation launch script
├── rebuild-and-test.sh    # Full workflow script
├── build-ps1/             # CMake build directory
│   ├── jcreborn.exe       # PS1 executable (output)
│   └── ...
├── cd_layout.xml          # CD-ROM layout configuration
├── jcreborn.bin           # CD image (output)
├── jcreborn.cue           # CD cue sheet (output)
├── jc_resources/          # Original Sierra data files
│   ├── RESOURCE.MAP
│   └── RESOURCE.001
├── cdrom_ps1.c/.h         # CD-ROM file I/O layer
├── graphics_ps1.c/.h      # PSn00bSDK GPU layer
├── sound_ps1.c/.h         # SPU audio layer
├── events_ps1.c/.h        # Controller input layer
└── ps1_stubs.c            # Missing libc functions
```

## Docker Container

The build uses a Docker container with PSn00bSDK:

- **Image**: `jc-reborn-ps1-dev:amd64`
- **Platform**: `linux/amd64` (required for compatibility)
- **SDK**: PSn00bSDK 0.24
- **Toolchain**: `mipsel-none-elf-gcc`

All build scripts handle the Docker invocation correctly. Never run Docker commands manually.

## DuckStation Emulator

- **Installation**: Flatpak (`org.duckstation.DuckStation`)
- **Launch**: Always use `./test-ps1.sh`
- **CD Image**: Must use absolute path to `.cue` file

The test script handles path resolution and background launching correctly.

## Troubleshooting

### "Docker command failed"
- Check that Docker is running: `docker ps`
- Check that the image exists: `docker images | grep jc-reborn-ps1-dev`

### "DuckStation won't launch"
- Check Flatpak installation: `flatpak list | grep DuckStation`
- Manually kill stuck instances: `pkill -9 -f duckstation`

### "Build fails with permission error"
- Check file permissions in `build-ps1/`
- Run clean rebuild: `./rebuild-and-test.sh clean`

### "CD image is stale"
- The scripts handle timestamps correctly
- If in doubt, run `./rebuild-and-test.sh` which rebuilds everything

## Debugging Colors

The PS1 build uses visual debugging (colored screens) since printf() doesn't work in DuckStation TTY:

- **RED** = Reached main()
- **GREEN** = CD-ROM initialized
- **CYAN** = File opened successfully
- **MAGENTA** = Resources parsed
- **BLUE** = Ready to render

Error colors:
- **YELLOW** = CD-ROM seek timeout
- **ORANGE** = CdRead failed
- **WHITE** = Read completion timeout
- **PINK** = CdSearchFile failed (file not found)
- **DARK RED** = No free CD file slots

## Remember

1. **ALWAYS** use the scripts, never run commands manually
2. **ALWAYS** run from the project root directory
3. **ALWAYS** use `./rebuild-and-test.sh` for the complete workflow
4. **NEVER** run docker, make, mkpsxiso, or duckstation commands directly

These scripts are the single source of truth for how to build and test the PS1 port.
