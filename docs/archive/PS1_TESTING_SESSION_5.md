# PS1 Testing Session 5 - Graphics Boot Investigation

**Date**: 2025-10-20
**Goal**: Get full game booting and rendering on PS1 from CD-ROM
**Status**: PARTIAL SUCCESS - Minimal test works, full game has C runtime issue

## Key Achievements

### 1. **Minimal Test Successfully Boots from CD-ROM! ✅**
- Created minimal test executable (ps1_minimal_main.c) that draws 3 colored rectangles
- **Resolution**: 640x480 interlaced mode
- **Boots successfully** from CD image in DuckStation
- **Graphics render correctly** - purple background with red/green/blue squares
- **Proves**: CD boot works, GPU init works, 640x480 mode works on PS1

### 2. **Identified Critical Bug: CdInit() Crash**
- **Problem**: Calling `CdInit()` when booting from CD-ROM causes immediate crash
- **Root Cause**: PS1 BIOS already initializes CD-ROM hardware when booting from CD. Calling `CdInit()` again re-initializes hardware in conflicting state
- **Evidence**: Adding `CdInit()` to working minimal test immediately broke it
- **Fix**: Removed `CdInit()` calls from:
  - jc_reborn.c:207
  - cdrom_ps1.c:40
  - Also removed CdSync() and CdControl() calls that depend on CdInit()

### 3. **Full Game Issue: Not Reaching main()**
- Full game (82KB) doesn't reach main() function
- Added printf at very start of main() - **no output in TTY console**
- Problem occurs BEFORE our code runs, during C runtime initialization
- Minimal test (8KB) reaches main() successfully

## Technical Details

### Working Configuration (Minimal Test)
```c
// ps1_minimal_main.c
InitHeap((void*)0x801fff00, 0x00100000);
// NO CdInit() call
ResetGraph(0);
SetVideoMode(MODE_NTSC);
// 640x480 double-buffered rendering
// Ordering tables + DrawOTag() pattern
```

### Memory Layout
**Full Game**:
- text: 73,860 bytes
- data: 6,806 bytes
- bss: 166,532 bytes (166KB of globals!)
- Total: ~247KB
- Load address: 0x80010000
- Stack: 0x801FFF00
- Entry: 0x8001dcc4 (_start)

**Minimal Test**:
- text + data + bss: ~8KB total
- Same load address and stack

### Files Modified
1. **jc_reborn.c**
   - Removed CdInit() call
   - Added early printf debug

2. **cdrom_ps1.c**
   - Commented out CdInit()
   - Removed CdSync() wait loop
   - Removed CdControl() mode setting
   - Function now only initializes internal state arrays

3. **ps1_minimal_main.c** (NEW)
   - Standalone working test
   - 640x480 rendering
   - Proves GPU/CD boot works

4. **cd_layout_minimal.xml** (NEW)
   - CD image layout for minimal test

5. **SYSTEM_MINIMAL.CNF** (NEW)
   - Boot config for minimal test

## Current Problem

**Full game hangs before main()** - This suggests one of:

1. **Large BSS section** (166KB) causing initialization timeout
2. **Global constructor** calling problematic code
3. **Stack/heap collision** during startup
4. **Linker script issue** with memory layout
5. **PSn00bSDK C runtime** incompatibility with our large executable

## Next Steps

### Immediate Debugging
1. **Compare executable headers** between working minimal and broken full game
2. **Check linker scripts** - minimal vs full game may use different memory layouts
3. **Reduce BSS size** - 166KB is huge, likely from large global arrays
4. **Test intermediate sizes** - gradually add source files to minimal test until it breaks
5. **Check for global constructors** - any C++ code or __attribute__((constructor))?

### Alternative Approaches
1. **Build full game without large globals** - make them malloc'd instead
2. **Use different linker script** - PSn00bSDK may have options for large programs
3. **Split into multiple executables** - load game code dynamically from CD
4. **Debug with PCSX-Redux** - has better debugging tools than DuckStation

## Code References

### Working Minimal Test
- Source: `/Users/hunterdavis/workspace/jc_reborn/ps1_minimal_main.c`
- Build: `/Users/hunterdavis/workspace/jc_reborn/build-minimal/`
- Executable: `ps1_minimal.exe` (8KB)
- CD Image: `ps1_minimal.cue/.bin`

### Full Game (Not Working)
- Build: `/Users/hunterdavis/workspace/jc_reborn/build-ps1/`
- Executable: `jcreborn.exe` (82KB)
- CD Image: `jcreborn.cue/.bin`
- **Problem**: Hangs before main(), no TTY output

## Lessons Learned

1. **Don't call CdInit() when booting from CD** - BIOS handles it
2. **Minimal tests are essential** - isolated the CD boot issue quickly
3. **640x480 mode works fine** - not a resolution problem
4. **Large executables may have issues** - PSn00bSDK might expect smaller programs
5. **TTY printf works from CD** - but only if you reach main()!

## Build Commands

### Minimal Test (Working)
```bash
cd build-minimal
cmake -DCMAKE_BUILD_TYPE=Release ..
make
# Creates ps1_minimal.exe

# Create CD image
mkpsxiso cd_layout_minimal.xml
# Creates ps1_minimal.cue/.bin

# Test in DuckStation
open -a DuckStation ps1_minimal.cue
```

### Full Game (Hangs)
```bash
cd build-ps1
make
# Creates jcreborn.exe

# Create CD image
mkpsxiso cd_layout.xml
# Creates jcreborn.cue/.bin

# Test in DuckStation (hangs at PlayStation logo)
open -a DuckStation jcreborn.cue
```

## Session Outcome

**Major Progress**:
- ✅ Proven CD boot works
- ✅ Proven 640x480 rendering works
- ✅ Identified and fixed CdInit() crash bug
- ✅ Created working minimal test as reference

**Remaining Issue**:
- ❌ Full game doesn't reach main() - C runtime initialization problem
- Need to investigate why large executable fails to start

**Confidence Level**: High that this is solvable - we have a working pattern (minimal test), just need to apply it to full game or reduce full game size.
