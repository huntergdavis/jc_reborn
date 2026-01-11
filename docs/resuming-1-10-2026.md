# Johnny Reborn PS1 Port - Development Handoff Document
**Date:** January 10, 2026
**Status:** Just applied sprite index fix - needs testing

## Project Overview

This is a PS1 port of "Johnny Reborn", an open-source engine for the classic Johnny Castaway screensaver by Sierra. The project successfully loads backgrounds and the island scene but animated sprites (Johnny walking) are not rendering.

## Machine Setup (KDE Neon)

### 1. Install Required Packages

```bash
sudo apt update
sudo apt install -y build-essential cmake git flatpak docker.io
sudo usermod -aG docker $USER
```

### 2. Install PSn00bSDK (PS1 Cross-Compiler)

```bash
# Download and extract PSn00bSDK 0.24
cd /opt
sudo wget https://github.com/Lameguy64/PSn00bSDK/releases/download/v0.24/PSn00bSDK-0.24-Linux.tar.gz
sudo tar -xzf PSn00bSDK-0.24-Linux.tar.gz
sudo mv PSn00bSDK-0.24-Linux psn00bsdk

# Add to PATH (add to ~/.bashrc)
export PATH="/opt/psn00bsdk/bin:$PATH"
export PSN00BSDK_LIBS="/opt/psn00bsdk/lib/libpsn00b"
```

### 3. Install DuckStation Emulator (via Flatpak)

```bash
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install flathub org.duckstation.DuckStation
```

### 4. Build Docker Image for PS1 Compilation

```bash
cd /path/to/jc_reborn
./scripts/build-docker-image.sh
```

This creates `jc-reborn-ps1-dev:amd64` Docker image with PSn00bSDK.

## Repository Structure

```
jc_reborn/
├── build-ps1/           # PS1 build output (jcreborn.elf, jcreborn.exe)
├── config/ps1/          # CD layout XML for mkpsxiso
├── jc_resources/        # Game resources
│   └── extracted/       # Pre-extracted BMP, SCR, TTM files
│       ├── bmp/         # Sprite sheets (JOHNWALK.BMP, TRUNK.BMP, etc.)
│       ├── scr/         # Background screens (OCEAN00.SCR, ISLETEMP.SCR)
│       └── ttm/         # Animation scripts (FISHWALK.TTM)
├── scripts/
│   ├── build-ps1.sh           # Build PS1 executable
│   ├── make-cd-image.sh       # Create CD image (jcreborn.bin/cue)
│   └── rebuild-and-let-run.sh # Full rebuild + launch emulator
├── graphics_ps1.c       # PS1 graphics implementation
├── cdrom_ps1.c          # PS1 CD-ROM loading
├── ttm.c                # TTM bytecode interpreter
└── jc_reborn.c          # Main entry point
```

## Build & Run Commands

**IMPORTANT:** Always use the provided scripts, not manual commands. Never use sudo.

### Full Rebuild and Test
```bash
./scripts/rebuild-and-let-run.sh noclean
```

This:
1. Builds the PS1 executable in Docker
2. Creates the CD image (jcreborn.bin/cue)
3. Launches DuckStation emulator
4. Takes screenshots after 30 and 40 seconds
5. Waits for manual emulator close

### Individual Steps (if needed)
```bash
# Build only
./scripts/build-ps1.sh noclean

# Create CD image only
./scripts/make-cd-image.sh

# Run emulator manually
flatpak run --filesystem="$(pwd)" org.duckstation.DuckStation -fastboot "$PWD/jcreborn.cue"
```

## Current State

### What Works
- Title screen displays (TITLE.RAW loaded from CD)
- Ocean background (OCEAN00.SCR - 640x480) loads correctly
- Island overlay (ISLETEMP.SCR - 640x350) loads correctly
- Tree trunk sprite renders (TRUNK.BMP)
- TTM bytecode loads and executes (FISHWALK.TTM)
- LOAD_IMAGE opcodes fire (confirmed via debug markers)
- grDrawSprite is called for JOHNWALK sprites (confirmed)

### What Doesn't Work
- **Johnny walking sprites don't render**
- grCompositeToBackground appears to not be called (no magenta debug pixels appear)

### Latest Fix Applied (UNTESTED)

**Problem identified**: JOHNWALK.BMP has 42 frames but we cap loading at 16 frames to save memory. The TTM bytecode calls DRAW_SPRITE with spriteNo values > 16, which caused early return in grDrawSprite.

**Fix applied** in `graphics_ps1.c`:
- `grDrawSprite()` (~line 1102): Changed sprite index check to use modulo
- `grDrawSpriteFlip()` (~line 1245): Same fix

```c
// OLD (broken):
if (spriteNo >= ttmSlot->numSprites[imageNo]) {
    return;  /* Sprite index out of range */
}
PS1Surface *sprite = ttmSlot->sprites[imageNo][spriteNo];

// NEW (fixed):
if (imageNo >= MAX_BMP_SLOTS || ttmSlot->numSprites[imageNo] == 0) {
    return;
}
uint16 actualSpriteNo = spriteNo % ttmSlot->numSprites[imageNo];
PS1Surface *sprite = ttmSlot->sprites[imageNo][actualSpriteNo];
```

This makes sprite indices wrap around (e.g., frame 17 becomes frame 1 with 16 loaded frames).

**RUN `./scripts/rebuild-and-let-run.sh noclean` TO TEST THIS FIX**

### Key Findings from Debugging

1. **Memory constraints**: PS1 has 2MB RAM. JOHNWALK.BMP has 42 frames at 64x78 pixels each (~420KB). Added 16-frame cap to prevent memory exhaustion.

2. **TTM execution works**:
   - First ttmPlay() loads background, yields at UPDATE
   - Subsequent calls execute sprite loading and drawing
   - LOAD_IMAGE opcode reaches grLoadBmp (confirmed with debug markers)
   - DRAW_SPRITE opcode reaches grDrawSprite (confirmed with yellow marker for imageNo==1)

3. **Sprite loading seems to work**: grLoadBmpRAM is called, allocates surfaces, converts 4-bit indexed to 15-bit direct color.

4. **The gap**: grDrawSprite is called, but grCompositeToBackground doesn't appear to execute (no magenta pixels at sprite positions).

### Current Debug State

There's a debug marker in `grCompositeToBackground()` (graphics_ps1.c ~line 932) that should draw a magenta pixel at each sprite position. It was NOT appearing before the fix, suggesting the sprite pointer lookup was failing.

### Files to Investigate

1. **graphics_ps1.c** - Key functions:
   - `grLoadBmpRAM()` (~line 780) - Loads sprites into RAM
   - `grDrawSprite()` (~line 1102) - Calls grCompositeToBackground
   - `grCompositeToBackground()` (~line 928) - Composites sprites to tiles

2. **ttm.c** - TTM interpreter:
   - `ttmPlay()` (~line 208) - Executes TTM bytecode
   - LOAD_IMAGE opcode handler (~line 427)
   - DRAW_SPRITE opcode handler (~line 398)

3. **jc_reborn.c** - Main loop (PS1 section ~line 390):
   - Calls grRestoreBgTiles() to reset background
   - Calls ttmPlay() to execute TTM
   - Calls grDrawBackground() to upload tiles

## Next Debugging Steps

If the fix doesn't work:

1. **Verify bgTile pointers exist**: In grCompositeToBackground, check if bgTile0/1/3/4 are non-NULL.

2. **Check sprite->pixels**: Add debug to verify sprite->pixels is non-NULL after the modulo fix.

3. **Verify the compositing loop**: Add a debug marker inside the pixel loop of grCompositeToBackground to confirm it runs.

## TTM Bytecode Reference (FISHWALK.TTM)

```
Offset 0x00: SET_PALETTE_SLOT 0
Offset 0x04: LOAD_PALETTE "JOHNCAST.PAL"
Offset 0x14: LOAD_SCREEN "ISLETEMP.SCR"
Offset 0x26: UPDATE (first yield)
Offset 0x28: TAG 19
...
Offset 0x40: SET_BMP_SLOT 2
Offset 0x44: LOAD_IMAGE "TRUNK.BMP"
Offset 0x52: SET_BMP_SLOT 0
Offset 0x56: LOAD_IMAGE "MJFISH1.BMP"
Offset 0x66: SET_BMP_SLOT 1
Offset 0x6A: LOAD_IMAGE "JOHNWALK.BMP"
Offset 0x7E: DRAW_SPRITE 400, 213, 3, 1  (Johnny at x=400, y=213, sprite 3, slot 1)
...
```

Slot assignments:
- Slot 0: MJFISH1.BMP (fish)
- Slot 1: JOHNWALK.BMP (Johnny walking - 42 frames, capped to 16)
- Slot 2: TRUNK.BMP (tree trunk - few frames)

## Debugging Tips

### Visual Debug Markers
To add a visible debug marker, use LoadImage directly to VRAM:
```c
static uint16 pixels[64];
uint16 color = (31 << 10) | (0 << 5) | 31;  /* Magenta in PS1 BGR */
for (int i = 0; i < 64; i++) pixels[i] = color;
RECT rect;
setRECT(&rect, x, y, 8, 8);
LoadImage(&rect, (uint32*)pixels);
DrawSync(0);
```

### PS1 Color Format
PS1 uses 15-bit BGR: `(B << 10) | (G << 5) | R` where each component is 0-31.

### Screenshots Location
```
~/.var/app/org.duckstation.DuckStation/config/duckstation/screenshots/
```

## Plan File Reference
See `.claude/plans/indexed-meandering-biscuit.md` for the original TTM rendering implementation plan.

## Git Status
Branch: `ps1`
Last commits focused on TTM-driven scene rendering and lazy-loading resources from CD.

## Contact/Notes
This is an active debugging session. The core rendering pipeline works (backgrounds, static sprites like tree trunk). The issue is specifically with animated sprites from JOHNWALK.BMP not appearing despite the code path being executed up to grDrawSprite.

**A fix was just applied** - the sprite index modulo fix should resolve the issue where TTM requests sprite frames > 16 but only 16 are loaded. Test with `./scripts/rebuild-and-let-run.sh noclean`.
