# PS1 Development Workflow

Build, test, and debug procedures for the PlayStation 1 port.

## Quick Reference

```bash
# Clean build
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project/build-ps1 && rm -rf * && cmake .. && make"

# Create CD image
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project && mkpsxiso cd_layout.xml"

# Test in DuckStation
open /tmp/DuckStation.app  # Then load jcreborn.cue
```

## Full Development Cycle

### 1. Make Code Changes

Edit source files in your local repository:
```bash
# PS1-specific files
vim graphics_ps1.c
vim sound_ps1.c
vim events_ps1.c

# Core engine files (if needed)
vim resource.c
vim ttm.c
```

### 2. Clean Build

**CRITICAL**: Always do a clean build to avoid stale object files!

```bash
# Option A: Using build script
./build-ps1.sh clean

# Option B: Manual clean build
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project && rm -rf build-ps1 && mkdir build-ps1 && \
           cd build-ps1 && \
           cmake -DCMAKE_TOOLCHAIN_FILE=/opt/psn00bsdk/lib/libpsn00b/cmake/toolchain.cmake .. && \
           make"
```

### 3. Check Build Output

```bash
# Verify executable was created
ls -lh build-ps1/jcreborn.elf

# Check executable size
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  mipsel-none-elf-size build-ps1/jcreborn.elf

# Expected output:
#    text    data     bss     dec     hex filename
#   73860    6806   38644  119310   1d20e build-ps1/jcreborn.elf
```

### 4. Create CD Image

```bash
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project && mkpsxiso cd_layout.xml"

# Verify CD image created
ls -lh jcreborn.bin jcreborn.cue
```

### 5. Test in Emulator

```bash
# Launch DuckStation
open /tmp/DuckStation.app  # macOS
# or
flatpak run org.duckstation.DuckStation  # Linux

# In DuckStation:
# 1. File → Start Disc
# 2. Select jcreborn.cue
# 3. Observe output
```

### 6. Debug Issues

**If it doesn't boot:**
- Check DuckStation TTY console for errors
- Look for visual debug colored screens
- Verify BIOS is configured correctly

**If it crashes:**
- Add more visual debug points (colored rectangles)
- Check memory usage with `mipsel-none-elf-size`
- Verify BSS section is < 50KB

## Visual Debugging

Since `printf()` doesn't work in DuckStation, use colored screens:

```c
// At critical points in your code
DRAWENV draw;
SetDefDrawEnv(&draw, 0, 0, 640, 480);

// Use different colors for different locations
setRGB0(&draw, 255, 0, 0);    // RED = reached main()
setRGB0(&draw, 0, 255, 0);    // GREEN = CD init OK
setRGB0(&draw, 0, 0, 255);    // BLUE = resources loaded
setRGB0(&draw, 255, 255, 0);  // YELLOW = graphics init OK

draw.isbg = 1;  // Enable background clear
PutDrawEnv(&draw);
VSync(0);  // Display for 1 frame
```

## Memory Analysis

### Check BSS Size

**CRITICAL**: Keep BSS < 50KB for PS1 stability!

```bash
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  mipsel-none-elf-size build-ps1/jcreborn.elf
```

If BSS > 50KB, convert static arrays to malloc:

```c
// BAD: Large static array
static uint8 buffer[32768];  // 32KB in BSS!

// GOOD: Heap allocation
uint8 *buffer = malloc(32768);  // 32KB on heap
```

### Memory Profiling

Run the memory test suite on desktop first:

```bash
# On desktop (SDL2)
make test-memory

# Analyze output for memory hotspots
```

## Common Build Errors

### "undefined reference to..."

**Cause**: Missing library or source file

**Fix**: Check CMakeLists.ps1.txt:
```cmake
target_link_libraries(jcreborn
    psxgpu psxcd psxspu psxapi psxgte psxsio psxpress
)
```

### "No such file or directory"

**Cause**: Missing include path

**Fix**: Add to CMakeLists.ps1.txt:
```cmake
include_directories(
    ${CMAKE_SOURCE_DIR}
    /opt/psn00bsdk/include
)
```

### "error: 'CdInit' undefined"

**Cause**: Missing psxcd library or header

**Fix**: Add `#include <psxcd.h>` and link psxcd library

## Testing Strategy

### Phase 1: Build Verification
- [ ] Docker image builds successfully
- [ ] CMake configuration succeeds
- [ ] Source files compile without errors
- [ ] Linker resolves all symbols
- [ ] CD image generation completes

### Phase 2: Boot Testing
- [ ] DuckStation loads CD image
- [ ] Executable boots without crash
- [ ] Visual debug colors appear
- [ ] No immediate errors

### Phase 3: Functional Testing
- [ ] Display background image
- [ ] Render one TTM animation
- [ ] Controller input responsive
- [ ] Multiple layer compositing
- [ ] Sprite rendering correct

### Phase 4: Performance Testing
- [ ] Frame rate ≥ 30 FPS
- [ ] Memory usage < 2MB
- [ ] VRAM usage < 1MB
- [ ] No stuttering or glitches

## Performance Profiling

### Frame Rate

Add frame counter to visual debug:

```c
static int frameCount = 0;
frameCount++;

// Every 60 frames (1 second at 60Hz)
if (frameCount % 60 == 0) {
    // Show frame count via colored screen
}
```

### VRAM Usage

Track VRAM allocation in graphics_ps1.c:

```c
// Current VRAM tracking
static uint16 nextVRAMX = 640;
static uint16 nextVRAMY = 4;

// Add usage counter
static uint32 vramUsed = 0;

// In grLoadBmp:
vramUsed += width * height * 2;  // 16-bit pixels
```

## Git Workflow

### Committing Changes

```bash
# Stage changes
git add graphics_ps1.c sound_ps1.c events_ps1.c

# Commit with descriptive message
git commit -m "PS1: Implement sprite flipping in grDrawSpriteFlip"

# Push to ps1 branch
git push origin ps1
```

### Creating Test Builds

```bash
# Create a test branch
git checkout -b ps1-test-sprites

# Make experimental changes
vim graphics_ps1.c

# Test thoroughly
./build-ps1.sh

# If successful, merge back
git checkout ps1
git merge ps1-test-sprites
```

## Critical PS1-Specific Rules

### DO NOT:
- ❌ Call `CdInit()` when booting from CD-ROM (causes crash)
- ❌ Use `printf()` for debugging (doesn't output to TTY)
- ❌ Create large static arrays (keep BSS < 50KB)
- ❌ Use `sudo` with Docker commands (breaks permissions)

### DO:
- ✅ Use visual debugging (colored screens)
- ✅ Use `malloc()` for large buffers
- ✅ Always clean build when testing
- ✅ Check BSS size after changes
- ✅ Test frequently in DuckStation

## Automated Build Script

The `build-ps1.sh` script automates the entire process:

```bash
#!/bin/bash
# Clean build for PS1

set -e

echo "=== PS1 Build Script ==="
echo

# Clean previous build
echo "1. Cleaning previous build..."
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project && rm -rf build-ps1"

# Build executable
echo "2. Building executable..."
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

# Check outputs
echo "4. Verifying outputs..."
ls -lh jcreborn.bin jcreborn.cue build-ps1/jcreborn.elf

echo
echo "=== Build Complete ==="
echo "Load jcreborn.cue in DuckStation to test"
```

## See Also

- [Toolchain Setup](toolchain-setup.md) - Setting up the development environment
- [Current Status](current-status.md) - What's implemented and what's next
- [Build System](build-system.md) - CMake and CD image details
