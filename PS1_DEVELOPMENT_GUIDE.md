# PS1 Development Guide - Johnny Reborn

**Last Updated**: 2025-10-20
**Branch**: `ps1`
**Status**: Visual debugging implemented, awaiting full game boot test

## Current State

### What's Working ✅
- **Minimal test** (3 colored squares) boots from CD and renders at 640x480
- CD-ROM boot process (without calling CdInit())
- GPU initialization and display at 640x480 interlaced
- Basic rendering (TILE primitives, ordering tables)
- CD image creation with mkpsxiso

### What's NOT Working ❌
- **Full game (jcreborn.exe)** hangs before reaching main()
- printf() does NOT output to DuckStation TTY console
- We don't know if it's crashing before main() or during C runtime init

### Visual Debugging Solution 🎨
Since printf() doesn't work, implemented **colored screen indicators**:
- **RED screen (2 sec)** = main() reached!
- **GREEN screen (2 sec)** = CD-ROM initialized
- **BLUE screen (2 sec)** = Resources parsed
- **PURPLE screen (2 sec)** = About to init graphics
- **YELLOW screen** = ERROR (CD-ROM init failed)

## Environment Setup (Linux)

### Prerequisites
```bash
# Install Docker
sudo apt-get update
sudo apt-get install docker.io docker-compose
sudo usermod -aG docker $USER
# Log out and back in for group changes

# Verify Docker
docker --version
docker run hello-world
```

### Build PS1 Development Container
```bash
cd /path/to/jc_reborn
docker build --platform linux/amd64 -f Dockerfile.ps1 -t jc-reborn-ps1-dev:amd64 .
```

**Important**: Must use `--platform linux/amd64` for PSn00bSDK compatibility.

### Container Contents
- PSn00bSDK 0.24 (PS1 homebrew SDK)
- mipsel-none-elf-gcc 12.3.0 (MIPS cross-compiler)
- mkpsxiso (CD image builder)
- elf2x (ELF to PS-EXE converter)

## Build Commands

### Minimal Test (Working)
```bash
# Clean build
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project && rm -rf build-minimal && mkdir build-minimal && \
           cp CMakeLists.minimal.txt build-minimal/CMakeLists.txt && \
           cp ps1_minimal_main.c build-minimal/ && \
           cd build-minimal && cmake . && make"

# Create CD image
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project && mkpsxiso cd_layout_minimal.xml"

# Output: ps1_minimal.cue / ps1_minimal.bin
```

### Full Game (Not Yet Working)
```bash
# Clean build
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project/build-ps1 && make clean && make"

# Create CD image
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project && mkpsxiso cd_layout.xml"

# Output: jcreborn.cue / jcreborn.bin
```

## Testing with DuckStation

### Linux Setup
```bash
# Download DuckStation AppImage
wget https://github.com/stenzek/duckstation/releases/latest/download/duckstation-qt-x64.AppImage
chmod +x duckstation-qt-x64.AppImage

# Run
./duckstation-qt-x64.AppImage
```

### Load CD Image
1. File → Start Disc
2. Select `jcreborn.cue` or `ps1_minimal.cue`
3. Watch for colored screens (visual debug indicators)

### Enable TTY Console (doesn't work yet, but for reference)
Settings → Console Settings → Enable TTY Output

## Key Technical Discoveries

### 1. CdInit() Crash Bug
**Problem**: Calling `CdInit()` when booting from CD-ROM causes immediate crash.
**Root Cause**: PS1 BIOS already initializes CD hardware. Calling `CdInit()` again creates conflicting state.
**Fix**: Removed all `CdInit()` calls from:
- jc_reborn.c:204
- cdrom_ps1.c:39

### 2. printf() Doesn't Output
**Problem**: Standard printf() calls don't appear in DuckStation TTY console.
**Workaround**: Visual debugging with colored screens.
**Theory**: PSn00bSDK's printf() may not output to TTY by default, or requires special initialization.

### 3. Large Executable Issue
**Minimal test**: 8KB, 70KB BSS → Works
**Full game**: 82KB text, 38KB BSS → Doesn't reach main()

**Hypothesis**: Large executable size or something in C runtime initialization is causing pre-main() crash.

## Memory Optimizations Applied

Converted large static arrays to malloc'd memory to reduce BSS:
- `primitiveBuffer[2][32768]` → malloc (graphics_ps1.c:40, :129-141)
- `cdSectorBuffer[32KB]` → malloc (cdrom_ps1.c:32, :47-56)
- `ttmSlots`/`ttmThreads` arrays → malloc (ads.c:88-92, :427-446)

**Result**: BSS reduced from 166KB to 38KB (77% reduction)

## File Structure

### PS1-Specific Files
```
jc_reborn/
├── Dockerfile.ps1              # PS1 build environment
├── CMakeLists.ps1.txt          # Full game CMake config
├── CMakeLists.minimal.txt      # Minimal test CMake config
├── cd_layout.xml               # CD image layout (full game)
├── cd_layout_minimal.xml       # CD image layout (minimal test)
├── SYSTEM.CNF                  # Boot config (full game)
├── SYSTEM_MINIMAL.CNF          # Boot config (minimal test)
├── graphics_ps1.c/h            # PS1 GPU abstraction
├── events_ps1.c/h              # PS1 input handling
├── sound_ps1.c/h               # PS1 SPU abstraction
├── cdrom_ps1.c/h               # CD-ROM file access
├── ps1_stubs.c                 # Missing libc functions
├── ps1_minimal_main.c          # Minimal working test
└── PS1_TESTING_SESSION_*.md    # Testing logs
```

### Build Artifacts (gitignored)
```
build-ps1/                      # Full game build
build-minimal/                  # Minimal test build
*.bin, *.cue                    # CD images
*.elf, *.exe                    # PS1 executables
```

## Next Steps (In Order)

### 1. Test Visual Debugging
```bash
# Build full game with visual debug markers
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project/build-ps1 && make clean && make"

# Create CD
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project && mkpsxiso cd_layout.xml"

# Test and note which colors appear
```

### 2. Based on Visual Debug Results

**If NO colors appear** (hangs before main):
- Linker script issue
- Entry point problem
- Heap initialization crash
- C runtime initialization bug

**If RED appears** (reached main):
- Problem is after main(), in our code
- Check which subsequent colors appear

**If GREEN appears** (CD init OK):
- CD-ROM code works
- Problem is later (resources or graphics)

**If BLUE appears** (resources parsed):
- Nearly everything works!
- Problem is in graphics init

### 3. Reduce Executable Size
If pre-main crash suspected:
- Split into multiple executables
- Load code dynamically from CD
- Use PSn00bSDK overlay system
- Profile what's causing large binary

### 4. Alternative Debugging
- Use PCSX-Redux emulator (better debugging tools)
- Add LED blink patterns if available
- Inject assembly breakpoints
- Build with debug symbols and use GDB stub

## Common Issues & Solutions

### Docker Permission Denied
```bash
sudo usermod -aG docker $USER
# Log out and back in
```

### Build Fails: "PSN00BSDK not found"
Container issue. Rebuild:
```bash
docker build --platform linux/amd64 -f Dockerfile.ps1 -t jc-reborn-ps1-dev:amd64 .
```

### CD Image Fails: "File not found"
Ensure .exe was created:
```bash
ls -la build-ps1/jcreborn.exe
```

If missing, check build output for errors.

### DuckStation Shows Black Screen
- Check CD loaded correctly (File → Start Disc)
- Try minimal test first to verify DuckStation works
- Enable "Fast Boot" in settings

## Resources

- **PSn00bSDK Docs**: https://github.com/Lameguy64/PSn00bSDK
- **PS1 Dev Wiki**: https://psx-spx.consoledev.net/
- **DuckStation**: https://github.com/stenzek/duckstation
- **PCSX-Redux**: https://github.com/grumpycoders/pcsx-redux

## Session History

See detailed testing logs:
- `PS1_TESTING_SESSION_4.md` - Graphics boot investigation
- `PS1_TESTING_SESSION_5.md` - CdInit bug discovery, minimal test success
- `PS1_PORT_STATUS.md` - Overall port status
- `PS1_PROJECT_SUMMARY.md` - Architecture and design

## Contact

If stuck, check:
1. Session logs (PS1_TESTING_SESSION_*.md)
2. git log on ps1 branch
3. Docker container logs
