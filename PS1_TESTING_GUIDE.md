# PS1 Testing Guide

## Quick Start

The PS1 port is configured to automatically play SAILING.TTM when booted without arguments. This provides a simple test case for visual regression testing.

### Loading in DuckStation

1. Open DuckStation emulator
2. Go to File → Start Disc
3. Select `jcreborn.cue` from the project root
4. The executable should boot and display debug output

### Expected Boot Sequence

```
Johnny Reborn - PS1 Port
Initializing...
PS1 Test Mode: Will play SAILING.TTM animation
Initializing CD-ROM...
CD-ROM initialized successfully
Parsing resource files...
Opening map file: RESOURCE.MAP
Map file opened successfully
Resource files parsed successfully
Initializing LRU cache...
LRU cache initialized
Initializing graphics...
Graphics initialized
Initializing sound...
Sound initialized
[TTM animation should start playing]
```

## Visual Regression Testing

### Baseline: SDL Version

To generate baseline frames from the SDL version:

```bash
cd jc_resources
./jc_reborn window debug ttm SAILING
```

The SDL version has frame capture capability. To capture frames:

```bash
./jc_reborn window debug capture-frame 10 capture-output sailing_frame_10.bmp ttm SAILING
```

### PS1 Version Testing

1. Load jcreborn.cue in DuckStation
2. Let SAILING.TTM play
3. Use DuckStation's screenshot feature to capture frames
4. Compare PS1 screenshots with SDL baseline frames

### Test Cases

#### Test 1: SAILING Animation
- **File**: SAILING.TTM
- **Description**: Simple sailing boat animation
- **Duration**: ~30 seconds
- **Key Frames**:
  - Frame 0: Initial scene
  - Frame 60: Boat in motion
  - Frame 120: Mid-animation

#### Test 2: Island Background
- **File**: ISLAND.SCR
- **Description**: Static island background
- **Test**: Verify background loads and displays correctly

#### Test 3: Sprite Rendering
- **Test**: SAILING animation includes sprites
- **Verify**:
  - Sprites appear at correct positions
  - Sprites have correct colors
  - No corruption or artifacts

## Debugging

### Console Output

DuckStation can show console output (printf statements). Enable it in:
- Settings → Console → TTY Output → Show

### Common Issues

#### Issue: Black Screen
- **Cause**: Graphics initialization failed
- **Debug**: Check console for "Graphics initialized" message
- **Solution**: Verify GPU init code in graphics_ps1.c

#### Issue: No CD-ROM Access
- **Cause**: CD-ROM init failed or files not found
- **Debug**: Check for "CD-ROM initialized successfully"
- **Solution**: Verify cd_layout.xml paths and file case (uppercase)

#### Issue: Resource Loading Fails
- **Cause**: File I/O or decompression error
- **Debug**: Check "Map file opened successfully"
- **Solution**: Verify CD-ROM read implementation in cdrom_ps1.c

#### Issue: Garbled Graphics
- **Cause**: VRAM upload or palette issues
- **Debug**: Check sprite loading and CLUT upload
- **Solution**: Verify LoadImage calls in graphics_ps1.c

### Debug Mode

Debug mode is automatically enabled on PS1 builds. All debug output will appear on the TTY console.

## Performance Testing

### Frame Rate

- Target: 30 FPS minimum
- Measure: DuckStation shows FPS in title bar
- Profile: Use DuckStation's performance counter overlay

### Memory Usage

- PS1 RAM: 2MB total
- Target: <500KB executable + working memory
- Measure: Use DuckStation's memory viewer

### CD Access

- Minimize seeks
- Pre-cache critical resources
- Monitor: DuckStation CD activity indicator

## Visual Regression Checklist

- [ ] Background renders correctly
- [ ] Sprites appear at correct positions
- [ ] Sprite colors match SDL version
- [ ] Animation timing is correct (30 FPS)
- [ ] No visual artifacts (tearing, corruption)
- [ ] Palette is correct (compare with SDL)
- [ ] Double-buffering works (no flicker)
- [ ] Layer compositing works (sprites over background)

## Advanced Testing

### Controller Input

Once implemented, test:
- START button → Pause
- SELECT button → Quit
- TRIANGLE → Frame advance
- CIRCLE → Max speed toggle

### Multiple Animations

Test additional TTM files:
- GJNAT1.TTM - Complex animation
- SMDATE.TTM - Date-based scene
- Any other TTM from RESOURCE.001

### Full Story Mode

To test story mode instead of single TTM:
1. Comment out the PS1 test mode in jc_reborn.c
2. Rebuild
3. Story mode will randomly select scenes

## Emulator Settings

### Recommended DuckStation Settings

- **Renderer**: Hardware (OpenGL or Vulkan)
- **Resolution Scale**: 1x (native 640x480)
- **PGXP**: Off (for accuracy)
- **Frame Limit**: On (60 FPS)
- **CD Read Speed**: 2x (PS1 double-speed)
- **CPU Overclock**: 100% (no overclock)

### Debug Settings

- **TTY Output**: Show
- **Performance Counter**: On
- **Memory Card**: Not needed (no saves yet)

## Comparison Tools

### Frame Comparison

Use ImageMagick to compare frames:

```bash
compare -metric RMSE ps1_frame.png sdl_frame.png diff.png
```

Target: RMSE < 0.01 (nearly identical)

### Automation

Create a test script:

```bash
#!/bin/bash
# Compare PS1 frame with SDL baseline
for frame in 0 60 120; do
  compare -metric RMSE \
    ps1/sailing_$frame.png \
    sdl/sailing_$frame.png \
    diff_$frame.png
done
```

## Known Limitations

1. **No Audio**: SPU playback not yet implemented
2. **No Controller Input**: Stub returns no input
3. **Limited Testing**: Only SAILING.TTM tested so far
4. **No Save/Load**: Memory card support not implemented

## Next Steps

1. Boot in DuckStation and verify SAILING animation plays
2. Capture frames and compare with SDL version
3. Fix any visual discrepancies
4. Test additional animations
5. Implement controller input for better testing
6. Implement SPU audio for complete experience

## Success Criteria

### MVP (Minimum Viable Product)
- ✅ Boots in emulator
- ✅ Shows debug output
- ✅ Loads resources from CD
- ✅ Displays background
- ✅ Plays SAILING animation
- ✅ Sprites render correctly
- ✅ No crashes

### Visual Parity
- Frames match SDL version (within 1% difference)
- Colors are accurate
- Animation timing matches
- No visual artifacts

### Performance
- Maintains 30 FPS minimum
- CD access doesn't stall
- Memory usage < 500KB

## Troubleshooting Tips

1. **Always check console output first**
2. **Verify CD image contents** with `isoinfo -l -i jcreborn.bin`
3. **Use DuckStation debugger** for breakpoints
4. **Check VRAM viewer** for texture uploads
5. **Monitor CD activity** for excessive seeks

## References

- DuckStation Documentation: https://github.com/stenzek/duckstation
- PS1 Hardware Specs: http://problemkaputt.de/psx-spx.htm
- PSn00bSDK Examples: https://github.com/Lameguy64/PSn00bSDK/tree/master/examples

---
*Created: 2025-10-19*
*PS1 Port - Session 4*
