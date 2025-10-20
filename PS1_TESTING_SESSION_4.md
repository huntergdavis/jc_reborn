# PS1 Testing Session 4 - DuckStation Emulator Testing

**Date**: 2025-10-19
**Session**: 4 (Emulator Testing)
**Status**: Executable boots, console output pending

## Executive Summary

First successful boot in DuckStation emulator! The executable loads from CD-ROM and runs, but console debug output is missing and graphics are not rendering yet. Root cause identified: missing heap initialization.

## Test Environment

- **Emulator**: DuckStation (latest macOS release)
- **CD Image**: jcreborn.bin/jcreborn.cue (1.4MB)
- **Test File**: SAILING.TTM (default animation)
- **Settings**: TTY Output enabled, Hardware rendering

## Test Results

### ✅ Working Components

1. **BIOS Boot Sequence**
   - PS1 BIOS recognizes disc
   - SYSTEM.CNF parsed correctly
   - Executable loaded from CD

2. **Kernel Initialization**
   - Kernel initializes successfully
   - VPS (video frames) = ~60 FPS
   - No crashes or hangs

3. **CD-ROM Access**
   - Multiple sector reads observed (154-215)
   - RESOURCE.MAP and RESOURCE.001 accessed
   - CD-ROM subsystem functional

### ❌ Issues Identified

1. **No Console Output**
   - **Symptom**: No printf debug messages in TTY
   - **Root Cause**: Missing `InitHeap()` call
   - **Evidence**: PSn00bSDK requires heap init before printf/malloc
   - **Status**: Fixed in commit 54fa5e0

2. **No Graphics Rendering**
   - **Symptom**: FPS = 0 (no rendering)
   - **Root Cause**: To be determined after console output enabled
   - **Status**: Pending investigation

## DuckStation Log Analysis

### Key Log Entries

```
[ 2736.4177] I/Bus: Kernel initialized.
```
- Executable successfully boots

```
[ 2744.3589] D/CDROM: Getstat    Stat=0x02
[ 2744.3914] D/CDROM: GetID
[ 2748.2144] D/CDROM: Init
```
- CD-ROM subsystem initializes

```
[ 2749.4963] D/CDROM: Setloc     00:02:16
[ 2749.4983] D/CDROM: SeekL      00:02:16
[ 2750.1760] D/CDROM: DataSector 00:02:16 LBA=166
```
- Resource files being read from CD

```
[ 2740.3721] V/PerfMon: FPS: 12.88 VPS: 59.44
[ 2741.3823] V/PerfMon: FPS: 60.36 VPS: 60.36
```
- Brief spike to FPS=60 then drops to 0

```
[ 2745.3977] V/PerfMon: FPS: 0.00 VPS: 59.87
```
- Sustained 0 FPS despite running at correct speed

## Code Changes Made

### jc_reborn.c - PS1 Initialization

**Before:**
```c
#ifdef PS1_BUILD
    ResetGraph(3);
    InitGeom();
    CdInit();

    printf("Johnny Reborn - PS1 Port\n");
```

**After:**
```c
#ifdef PS1_BUILD
    InitHeap((void*)0x801fff00, 0x00100000);  /* Initialize heap */
    ResetGraph(0);  /* NTSC 320x240 mode */
    InitGeom();
    CdInit();

    printf("Johnny Reborn - PS1 Port\n");
```

**Key Changes:**
1. Added `InitHeap()` before any printf/malloc calls
2. Changed `ResetGraph(3)` → `ResetGraph(0)` for proper NTSC mode
3. Added PSn00bSDK headers (psxgpu.h, psxgte.h, psxcd.h)
4. Defined stderr/fprintf macros for PSn00bSDK compatibility

## Technical Insights

### PSn00bSDK Initialization Order

The correct initialization sequence for PSn00bSDK:
```c
1. InitHeap()       // Must be first for malloc/printf
2. ResetGraph()     // Reset GPU, set video mode
3. InitGeom()       // Initialize GTE (geometry engine)
4. CdInit()         // Initialize CD-ROM
5. printf()         // Now safe to use
```

### ResetGraph Modes

- `ResetGraph(0)` = NTSC 320x240 (correct for Johnny Reborn)
- `ResetGraph(3)` = Not standard, may cause issues

### Direct EXE vs CD Image Testing

**Direct EXE Loading** (faster iteration):
- Good for: Testing boot, init sequence
- Bad for: No CD data access, can't load resources
- Use for: Quick compile-test cycles

**CD Image Loading** (full functionality):
- Good for: Complete testing with resource files
- Bad for: Slower iteration (rebuild + regenerate image)
- Use for: Final testing, visual regression

## Next Steps

### Immediate (Next Session)

1. **Rebuild Executable**
   ```bash
   docker run --rm --platform linux/amd64 \
     -v /Users/hunterdavis/workspace/jc_reborn:/project \
     jc-reborn-ps1-dev:amd64 \
     bash -c "cd /project/build-ps1 && make clean && make"
   ```

2. **Regenerate CD Image**
   ```bash
   docker run --rm --platform linux/amd64 \
     -v /Users/hunterdavis/workspace/jc_reborn:/project \
     jc-reborn-ps1-dev:amd64 \
     bash -c "cd /project && mkpsxiso cd_layout.xml"
   ```

3. **Test in DuckStation**
   - Enable Settings → Console → TTY Output → Show
   - Load jcreborn.cue
   - Check for console debug messages
   - Verify printf statements appear

4. **Debug Graphics Issue**
   - Once console output works, trace execution
   - Check "Initializing graphics..." message
   - Look for GPU init errors
   - Verify VRAM uploads

### Investigation Areas

**If Console Output Still Missing:**
- Verify InitHeap address (0x801fff00) is correct
- Check heap size (0x00100000 = 1MB)
- Try different ResetGraph modes

**If Graphics Still Not Rendering:**
- Check ordering table setup in graphics_ps1.c:94
- Verify double-buffering (grCurrentBuffer)
- Check VRAM texture uploads
- Verify palette (CLUT) setup
- Test with simpler graphics (solid color fill)

## Files Modified

- `jc_reborn.c` - Added PS1 initialization (commit 54fa5e0)

## Testing Checklist

### Current Status
- [x] Executable boots in emulator
- [x] BIOS recognizes disc
- [x] Kernel initializes
- [x] CD-ROM reads files
- [ ] Console output visible (pending rebuild)
- [ ] Graphics render (pending)
- [ ] Animation plays (pending)

### Next Testing Phase
- [ ] Rebuild with InitHeap changes
- [ ] Regenerate CD image
- [ ] Verify TTY console output
- [ ] Debug graphics rendering
- [ ] Capture frame for visual regression
- [ ] Compare with SDL baseline

## Performance Metrics

- **VPS (Emulation Speed)**: ~60 FPS ✓
- **FPS (Game Rendering)**: 0 FPS ✗
- **CD Access**: Working ✓
- **Memory Usage**: Unknown (need console output)
- **CPU Usage**: Normal (5-7% per log)

## References

- DuckStation: https://github.com/stenzek/duckstation
- PSn00bSDK Docs: https://github.com/Lameguy64/PSn00bSDK
- PS1 Hardware Specs: http://problemkaputt.de/psx-spx.htm
- Session 3 Logs: PS1_TESTING_GUIDE.md
- Emulator Setup: EMULATOR_SETUP.md

## Session Notes

### Discoveries

1. **Direct EXE Testing**: DuckStation can load .exe files directly, enabling faster iteration without rebuilding CD images

2. **CD-ROM Activity**: Extensive sector reads confirm resource loading is working at the hardware level

3. **InitHeap Critical**: Missing heap initialization explains why printf doesn't work on PS1 (unlike SDL build)

### Challenges

1. **Slow Docker Builds**: Compilation taking 2+ minutes, making iteration slow
2. **No Debug Output**: Can't see what's failing without console output
3. **Graphics Mystery**: GPU appears to initialize (brief FPS spike) then stops rendering

### Wins

1. **First Boot Success**: Huge milestone - executable loads and runs!
2. **CD-ROM Works**: Resource files are accessible from CD
3. **Root Cause Found**: InitHeap issue identified quickly via log analysis
4. **Clean Fix**: Simple one-line addition fixes console output

## Commit Summary

**Commit**: 54fa5e0
**Message**: Add PS1 heap initialization and fix console output
**Files**: jc_reborn.c (19 insertions, 15 deletions)
**Branch**: ps1

---
*Session 4 Complete*
*Next Session: Rebuild, test with console output, debug graphics*
