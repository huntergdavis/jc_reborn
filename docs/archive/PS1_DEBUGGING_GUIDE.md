# PS1 Debugging Guide - Standardized Workflows

**Purpose**: Repeatable debugging workflows for PS1 port development
**Last Updated**: 2025-11-08
**Branch**: `ps1`

---

## Table of Contents

1. [Setup](#setup)
2. [Build Analysis](#build-analysis)
3. [Emulator Testing](#emulator-testing)
4. [Advanced Debugging with PCSX-Redux](#advanced-debugging-with-pcsx-redux)
5. [Common Issues](#common-issues)

---

## Setup

### First-Time Setup (Kubuntu/Ubuntu)

```bash
# 1. Install Docker
./setup-docker.sh

# 2. Log out and log back in (required for Docker group)

# 3. Build PS1 development Docker image
./build-docker-image.sh

# 4. Install DuckStation emulator (if not already installed)
flatpak install flathub org.duckstation.DuckStation

# 5. (Optional) Install PCSX-Redux for advanced debugging
# See "Advanced Debugging" section below
```

---

## Build Analysis

### Standard Build + Analysis Workflow

```bash
# 1. Build the executable
./build-ps1.sh

# 2. Analyze build output
./analyze-build.sh

# 3. Check for issues
# - BSS section should be < 50 KB
# - Text section should be < 100 KB
# - Total should be < 256 KB
```

### Clean Build

```bash
# Clean and rebuild from scratch
./build-ps1.sh clean
./analyze-build.sh
```

### Analyzing Specific Builds

```bash
# Analyze minimal test build
./analyze-build.sh build-minimal/ps1_minimal.elf

# Analyze full game build
./analyze-build.sh build-ps1/jcreborn.elf
```

### Expected Output

**Good build (likely to boot)**:
```
BSS section:  38912 bytes (38 KB)
Text section: 73860 bytes (72 KB)
Total:        118678 bytes (116 KB)

✓ No obvious size issues detected
```

**Problematic build (may not boot)**:
```
BSS section:  166532 bytes (162 KB)
Text section: 82144 bytes (80 KB)
Total:        254482 bytes (248 KB)

⚠️  LARGE BSS: 166532 bytes (162 KB)
   BSS > 50KB may cause boot issues on PS1
```

---

## Emulator Testing

### DuckStation (Primary Testing)

**Quick Test**:
```bash
# Build, create CD image, and launch emulator
./rebuild-and-test.sh
```

**Step-by-Step Test**:
```bash
# 1. Build executable
./build-ps1.sh

# 2. Create CD image
./make-cd-image.sh

# 3. Launch DuckStation
./test-ps1.sh
```

**Automated Test with Screenshot**:
```bash
# Run test for 13 seconds and capture screenshot
./auto-test-ps1.sh

# Run test for custom duration (30 seconds)
./auto-test-ps1.sh 30
```

### DuckStation Console Output

**Enable TTY Console** (for printf debugging):
1. Launch DuckStation
2. Settings → Console Settings
3. Enable "Enable TTY Output"
4. Window → Developer → CPU Debugger → Show Console Output

**Note**: printf() may not work from CD-ROM boot. Use visual debugging instead.

---

## Visual Debugging

Since printf() often doesn't work when booting from CD-ROM, use colored screen checkpoints:

### Adding Visual Checkpoints

```c
#include <psxgpu.h>
#include <psxapi.h>

void checkpoint(int r, int g, int b, int frames) {
    ResetGraph(0);
    SetVideoMode(MODE_NTSC);
    DRAWENV draw;
    SetDefDrawEnv(&draw, 0, 0, 640, 480);
    setRGB0(&draw, r, g, b);
    draw.isbg = 1;
    PutDrawEnv(&draw);
    SetDispMask(1);
    for (int i = 0; i < frames; i++) {
        VSync(0);
    }
}

int main(void) {
    checkpoint(255, 0, 0, 60);      // RED = main() reached

    // ... initialization code ...

    checkpoint(0, 255, 0, 60);      // GREEN = init complete

    // ... main loop ...
}
```

### Standard Color Codes

| Color | RGB | Meaning |
|-------|-----|---------|
| **RED** | 255,0,0 | Entry point reached |
| **GREEN** | 0,255,0 | Initialization complete |
| **BLUE** | 0,0,255 | Resources loaded |
| **YELLOW** | 255,255,0 | Graphics initialized |
| **CYAN** | 0,255,255 | Main loop started |
| **MAGENTA** | 255,0,255 | Checkpoint marker |
| **WHITE** | 255,255,255 | Error state |
| **ORANGE** | 255,128,0 | Warning state |

**Display Duration**: 60 frames = 1 second (NTSC)

---

## Advanced Debugging with PCSX-Redux

### Why PCSX-Redux?

PCSX-Redux offers superior debugging tools compared to DuckStation:
- **GDB debugger integration** - Set breakpoints, step through code
- **Memory viewer** - Inspect RAM/VRAM in real-time
- **Assembly viewer** - See disassembly with source correlation
- **TTY console** - Reliable printf() output
- **CPU profiler** - Identify performance bottlenecks

### Installing PCSX-Redux (Ubuntu/Kubuntu)

**Method 1: AppImage** (Recommended)
```bash
# Download latest AppImage
cd ~/Downloads
wget https://github.com/grumpycoders/pcsx-redux/releases/latest/download/PCSX-Redux-x86_64.AppImage

# Make executable
chmod +x PCSX-Redux-x86_64.AppImage

# Move to /opt for system-wide access
sudo mv PCSX-Redux-x86_64.AppImage /opt/pcsx-redux
sudo ln -s /opt/pcsx-redux /usr/local/bin/pcsx-redux

# Test
pcsx-redux
```

**Method 2: Build from Source**
```bash
# Install dependencies
sudo apt-get update
sudo apt-get install -y build-essential git cmake ninja-build \
    libsdl2-dev libfreetype6-dev libavcodec-dev libavformat-dev \
    libavutil-dev libswresample-dev libuv1-dev libglew-dev

# Clone repository
git clone https://github.com/grumpycoders/pcsx-redux.git
cd pcsx-redux
git submodule update --init --recursive

# Build
make -j$(nproc)

# Binary will be in: ./src/pcsx-redux
```

### PCSX-Redux Testing Workflow

```bash
# 1. Build and create CD image
./rebuild-and-test.sh

# 2. Launch PCSX-Redux with CD image
pcsx-redux -cdrom jcreborn.cue -run

# Or via desktop
pcsx-redux
# File → Open Disc Image → Select jcreborn.cue
```

### PCSX-Redux Debugging Features

**Enable TTY Console**:
- Configuration → Emulation → Enable TTY Console
- View → Show TTY Console

**Memory Viewer**:
- Debug → Memory Editor
- Useful addresses:
  - 0x80010000: Program code start
  - 0x1F800000: Scratchpad RAM
  - 0x1F801000: Hardware registers

**CPU Debugger**:
- Debug → Assembly Debugger
- Set breakpoints by clicking line numbers
- Step through code with F10/F11

**GDB Remote Debugging**:
```bash
# 1. Enable GDB server in PCSX-Redux
# Configuration → Debug → Enable GDB Server (port 3333)

# 2. In another terminal, connect GDB
mipsel-none-elf-gdb build-ps1/jcreborn.elf
(gdb) target remote localhost:3333
(gdb) break main
(gdb) continue
```

---

## Common Issues

### Issue 1: Executable Doesn't Boot

**Symptoms**:
- Black screen after PlayStation logo
- No colored checkpoint screens
- No TTY output

**Diagnosis**:
```bash
./analyze-build.sh
# Check for:
# - BSS > 50 KB
# - Total size > 256 KB
```

**Solutions**:
1. **Reduce BSS size** - Convert static arrays to malloc:
   ```c
   // Before (static - goes in BSS)
   static uint8_t buffer[32768];

   // After (dynamic - allocated at runtime)
   uint8_t *buffer = malloc(32768);
   ```

2. **Check for large global arrays** in:
   - resource.c (resource arrays)
   - graphics_ps1.c (primitive buffers)
   - cdrom_ps1.c (sector buffers)

3. **Test with minimal build**:
   ```bash
   # Build minimal test to verify toolchain
   cd build-minimal
   make
   ```

### Issue 2: CdInit() Crash

**Symptoms**:
- Works in minimal test
- Crashes in full game when CD-ROM accessed

**Cause**: PS1 BIOS already initializes CD-ROM when booting from CD. Calling `CdInit()` again causes hardware conflict.

**Solution**: Remove ALL `CdInit()` calls when booting from CD-ROM.

**Check files**:
- jc_reborn.c
- cdrom_ps1.c
- Any custom initialization code

### Issue 3: FntLoad/FntOpen Hang

**Symptoms**:
- Game boots and shows initial checkpoint screen
- Hangs on solid color screen (often RED) for entire test duration
- Code appears to reach main() but stops progressing
- VSync() calls hang indefinitely

**Cause**: PSX BIOS font functions `FntLoad()` and `FntOpen()` are unreliable when booting from CD-ROM. These functions may hang or fail silently, blocking all subsequent execution.

**Affected Functions**:
- `FntLoad(960, 0)` - Load BIOS font into VRAM
- `FntOpen(x, y, w, h, ...)` - Create font stream
- Any ps1Debug system that uses these functions

**Solution 1: Use Busy-Wait Instead of VSync**

VSync() can hang if GPU isn't properly initialized. Replace with busy-wait loops:

```c
// Before (hangs)
for (int i = 0; i < 120; i++) {
    VSync(0);
}

// After (works)
for (volatile int i = 0; i < 3000000; i++) {
    /* Busy wait ~1 second */
}
```

**Solution 2: Disable ps1Debug System**

Comment out all ps1DebugInit/Print/Flush calls and use simple colored screens:

```c
// Before (hangs)
ps1DebugInit();
ps1DebugPrint("Checkpoint");
ps1DebugFlush();

// After (works)
ResetGraph(0);
SetVideoMode(MODE_NTSC);
DRAWENV draw;
SetDefDrawEnv(&draw, 0, 0, 640, 480);
setRGB0(&draw, 0, 255, 0);  /* GREEN */
draw.isbg = 1;
PutDrawEnv(&draw);
SetDispMask(1);
for (volatile int i = 0; i < 3000000; i++);  /* Wait */
```

**Solution 3: Minimal GPU Init**

Use only these PSn00bSDK GPU functions (confirmed working):
- `ResetGraph(0)`
- `SetVideoMode(MODE_NTSC)`
- `SetDefDrawEnv(&draw, x, y, w, h)`
- `setRGB0(&draw, r, g, b)`
- `PutDrawEnv(&draw)`
- `SetDispMask(1)`

Avoid these until after main graphics init:
- `FntLoad()` - Unreliable
- `FntOpen()` - Unreliable
- `VSync()` - Can hang before full init
- `SetDispMask(DISP_INTERLACE)` - Wrong usage (SetDispMask takes 0/1 only)

**Testing Notes** (2025-11-15):

Successfully tested progression:
1. RED screen - main() reached ✅
2. GREEN screen - basic GPU working ✅
3. CYAN screen - continued execution ✅
4. YELLOW screen - pre-cdromInit ✅
5. WHITE screen - about to call cdromInit ✅
6. BLUE screen - cdromInit succeeded ✅
7. ORANGE screen - about to parseResourceFiles ✅
8. MAGENTA screen - parseResourceFiles succeeded ✅
9. PINK screen - about to graphicsInit ✅
10. BLACK screen - render loop working (600 frames) ✅

All systems functional once ps1Debug bypassed.

### Issue 4: printf() Not Working

**Cause**: TTY console output doesn't always work when booting from CD-ROM.

**Solution**: Use visual debugging (colored screens) instead.

See "Visual Debugging" section above.

### Issue 4: Docker Permission Denied

**Symptoms**:
```
permission denied while trying to connect to the Docker daemon socket
```

**Solution**:
```bash
# Add user to docker group
sudo usermod -aG docker $USER

# Log out and log back in, then:
newgrp docker

# Or restart system
```

### Issue 5: Build Succeeds but CD Image Fails

**Symptoms**:
- jcreborn.exe exists and looks good
- mkpsxiso fails or creates empty image

**Diagnosis**:
```bash
# Check CD layout file
cat cd_layout.xml

# Check if resource files exist
ls -lh jc_resources/
```

**Solution**:
- Verify all files in cd_layout.xml exist
- Check file paths are correct (absolute vs relative)
- Ensure RESOURCE.MAP and RESOURCE.001 are present

---

## Debugging Workflow Summary

**Standard testing cycle**:
```bash
./build-ps1.sh              # Build
./analyze-build.sh          # Check for issues
./make-cd-image.sh          # Create CD
./test-ps1.sh               # Test in DuckStation
```

**Full automated cycle**:
```bash
./rebuild-and-test.sh       # All-in-one
```

**With analysis and screenshot**:
```bash
./build-ps1.sh
./analyze-build.sh
./make-cd-image.sh
./auto-test-ps1.sh 20       # Test for 20 seconds + screenshot
```

**Advanced debugging**:
```bash
./rebuild-and-test.sh
pcsx-redux -cdrom jcreborn.cue -run
# Use Debug menu for breakpoints, memory inspection
```

---

## Tips for Effective Debugging

1. **Always run analyze-build.sh** after compilation
2. **Use visual checkpoints** liberally (every major function)
3. **Keep minimal test working** as a reference
4. **Test incrementally** - add features one at a time
5. **Check BSS size** frequently during development
6. **Use PCSX-Redux** for complex debugging scenarios
7. **Document findings** in PS1_TESTING_SESSION_*.md files

---

## References

- DuckStation: https://www.duckstation.org/
- PCSX-Redux: https://github.com/grumpycoders/pcsx-redux
- PSn00bSDK: https://github.com/Lameguy64/PSn00bSDK
- PS1 Development Resources: https://psx.arthus.net/

---

**Last Updated**: 2025-11-08
**Maintained By**: Johnny Reborn PS1 Port Team
