# PS1 Emulator Setup Guide

## Quick Start - DuckStation Installation

### macOS

```bash
# Download latest release
curl -L -o /tmp/duckstation-mac-release.zip \
  https://github.com/stenzek/duckstation/releases/latest/download/duckstation-mac-release.zip

# Extract
cd /tmp
unzip duckstation-mac-release.zip

# Move to Applications
mv DuckStation.app /Applications/

# Launch
open /Applications/DuckStation.app
```

Or download manually from: https://github.com/stenzek/duckstation/releases

### Initial Setup

1. **First Launch**
   - Accept permissions if prompted
   - Skip BIOS setup (homebrew doesn't need it)
   - Go to Settings

2. **Configure Settings**
   - **Display**: Hardware (OpenGL/Vulkan)
   - **Resolution**: 1x native
   - **Audio**: Default
   - **Console**: Region = Auto
   - **BIOS**: Not required for homebrew

3. **Enable TTY Output**
   - Settings → Console → TTY Output → Show
   - This shows printf debug output!

## Loading Johnny Reborn

1. File → Start Disc
2. Navigate to: `/Users/hunterdavis/workspace/jc_reborn/`
3. Select: `jcreborn.cue`
4. Watch console output!

## Expected Boot Sequence

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
[Animation starts]
```

## Troubleshooting

### Black Screen
- Check TTY output for errors
- Verify "Graphics initialized" appears
- Check DuckStation log (View → Show Log)

### "Cannot open map file"
- CD-ROM init failed
- Check cd_layout.xml paths
- Verify files are on CD image:
  ```bash
  isoinfo -l -i jcreborn.bin
  ```

### Crash/Hang
- Look for last message before hang
- Use DuckStation debugger (System → CPU Debugger)
- Check for infinite loops in code

### No Graphics
- GPU init failed
- Check ordering table setup
- Verify VRAM allocations

## Debugging Features

### DuckStation Debugger
- System → CPU Debugger
- Set breakpoints
- Step through code
- View registers/memory

### Frame Capture
- System → Dump RAM
- System → Dump VRAM
- Tools → Capture Frame

### Performance
- View → Show FPS
- View → Show Performance Counter
- Target: 30-60 FPS

## Visual Regression Testing

### Capture PS1 Frame
1. Let SAILING animation play
2. Pause at frame 60
3. System → Capture Frame
4. Save as `ps1_sailing_60.png`

### Compare with SDL
1. Build SDL version:
   ```bash
   cd jc_resources
   make
   ./jc_reborn window debug ttm SAILING
   ```

2. Capture frame 60 (if supported):
   ```bash
   ./jc_reborn window capture-frame 60 capture-output sdl_sailing_60.bmp ttm SAILING
   ```

3. Compare:
   ```bash
   # Convert BMP to PNG if needed
   convert sdl_sailing_60.bmp sdl_sailing_60.png

   # Compare
   compare -metric RMSE ps1_sailing_60.png sdl_sailing_60.png diff.png
   ```

## Alternative Emulators

### PCSX-Reloaded (Older, simpler)
```bash
brew install --cask pcsx
```
- Simpler but less accurate
- Good for quick tests

### Mednafen (Command-line)
```bash
brew install mednafen
mednafen jcreborn.cue
```
- Very accurate
- No GUI
- Good for automated testing

## CD Image Inspection

### View Contents
```bash
isoinfo -l -i jcreborn.bin
```

Expected output:
```
/JCREBORN.EXE;1
/RESOURCE.MAP;1
/RESOURCE.001;1
```

### Extract File
```bash
isoinfo -i jcreborn.bin -x /JCREBORN.EXE > extracted.exe
```

### Verify Executable
```bash
file extracted.exe
hexdump -C extracted.exe | head -20
```

Should show PS-EXE header.

## Next Steps After Boot

1. **Verify Console Output**
   - All init messages appear
   - No error messages
   - Graphics and sound init successfully

2. **Check Visual Display**
   - Screen clears (not black)
   - Background loads
   - Sprites appear

3. **Test Animation**
   - SAILING plays
   - Timing is correct (~30 FPS)
   - No glitches or artifacts

4. **Compare with SDL**
   - Capture same frames
   - Visual diff should be minimal
   - Colors should match

5. **Test Other Animations**
   - Try different TTM files
   - Test full story mode
   - Verify all features work

## Known Limitations

- No audio (SPU not implemented)
- No controller input (stub only)
- Debug mode always on
- Single TTM test mode only

## Resources

- DuckStation: https://github.com/stenzek/duckstation
- PS1 Specs: http://problemkaputt.de/psx-spx.htm
- PSn00bSDK: https://github.com/Lameguy64/PSn00bSDK

---
*Ready for emulator testing!*
*CD Image: jcreborn.bin (1.4MB)*
*Default Test: SAILING.TTM animation*
