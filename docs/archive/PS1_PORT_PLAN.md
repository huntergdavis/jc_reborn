# Johnny Reborn - PlayStation 1 Port Plan

## Overview

This document outlines the plan to port Johnny Reborn to the PlayStation 1 using PSn00bSDK. The 4mb2025 branch has achieved 350KB memory usage, which is well under the PS1's constraints (2MB system RAM, 1MB VRAM).

## Technical Specifications - PlayStation 1

- **CPU**: MIPS R3000A @ 33.8688 MHz
- **RAM**: 2MB system RAM
- **VRAM**: 1MB video RAM
- **GPU**: Supports 640x480 resolution (matches Johnny Reborn's native resolution!)
- **Storage**: CD-ROM (650MB capacity)
- **Audio**: SPU with 24 channels, 512KB SPU RAM

## Development Tools

### SDK: PSn00bSDK
- Modern open-source PS1 SDK by Lameguy64
- Uses CMake build system
- Includes mkpsxiso for CD image creation
- Compatible with Sony SDK library syntax
- Full GPU support (lines, polygons, sprites, DMA transfers)
- CD-ROM library with ISO9660 filesystem parser
- SPU audio support
- Controller input handling

### Emulator: DuckStation
- Best modern PS1 emulator for macOS (Intel/Apple Silicon)
- Active development in 2025
- Available via GitHub releases
- Accurate emulation for testing

### CD Image Tool: mkpsxiso
- Included with PSn00bSDK
- Creates ISO or BIN+CUE images
- Supports mixed-mode CD-XA
- Handles CDDA audio tracks
- Automatically injects Sony license data

## Installation Steps

### 1. Install Prerequisites (macOS)
```bash
# Install CMake via Homebrew
brew install cmake

# Install MIPS GCC toolchain (mipsel-none-elf-gcc)
# Note: May need to build from source - PSn00bSDK provides instructions
```

### 2. Build PSn00bSDK
```bash
# Clone PSn00bSDK repository
git clone https://github.com/Lameguy64/PSn00bSDK.git
cd PSn00bSDK

# Build and install (follow PSn00bSDK installation.md)
mkdir build && cd build
cmake ..
make
sudo make install
```

### 3. Install DuckStation Emulator
- Download from: https://github.com/stenzek/duckstation/releases
- Universal binary for macOS (Intel + Apple Silicon)

## Port Architecture

### Files to Modify/Replace

1. **graphics.c/h** - Replace SDL2 with PSn00bSDK GPU functions
   - Replace SDL surfaces with PS1 framebuffers/VRAM
   - Port blitting operations to PS1 sprite/polygon primitives
   - Adapt palette operations to PS1 CLUT system
   - Use ordering tables for rendering

2. **sound.c/h** - Replace SDL_mixer with PSn00bSDK SPU
   - Port WAV playback to SPU functions
   - Manage 512KB SPU RAM constraint
   - Use hardware audio mixing (24 channels available)

3. **events.c/h** - Replace SDL events with PS1 controller input
   - Map PSX controller buttons to hotkeys
   - Handle controller polling

4. **main.c** - Remove SDL initialization, add PSn00bSDK init
   - Initialize PS1 hardware subsystems
   - Set up video mode (640x480)
   - Initialize CD-ROM subsystem

### Files That Should Work As-Is

The following files are pure C with no SDL dependencies:
- **resource.c/h** - Resource loading (but needs CD-ROM I/O adaptation)
- **uncompress.c/h** - LZW/RLE decompression
- **ttm.c/h** - TTM animation engine
- **ads.c/h** - ADS scene interpreter
- **story.c/h** - Story orchestration
- **walk.c/h** - Walk system
- **calcpath.c/h** - Pathfinding
- **island.c/h** - Island generation
- **utils.c/h** - Utility functions
- **bench.c/h** - Benchmarking (may need timer adaptation)
- **config.c/h** - Configuration

### CD-ROM I/O Strategy

PSn00bSDK uses a different I/O model than stdio:
- No FILE* handles
- Read files by locating position via ISO9660 filesystem
- Seek to position and read sectors
- Asynchronous reading supported

**Adaptation needed in resource.c:**
- Replace `fopen/fread/fseek` with PSn00bSDK CD-ROM functions
- Locate RESOURCE.MAP and RESOURCE.001 on CD
- Cache frequently accessed resource data in RAM
- Consider streaming larger resources from CD during playback

### Memory Layout

**System RAM (2MB):**
- Code + data: ~200KB (estimated)
- LRU resource cache: 1.5MB (plenty for 350KB peak usage)
- Stack/heap: remaining

**VRAM (1MB):**
- Framebuffers (double-buffered 640x480): 600KB (if using 16-bit color)
- Or: 300KB for 8-bit indexed color (better fit)
- Texture cache: remaining VRAM
- CLUTs (color lookup tables): minimal space

**SPU RAM (512KB):**
- Sound effects (sound0.wav - sound24.wav)
- May need to stream from CD or use compressed audio

## Graphics System Port Details

### SDL2 → PSn00bSDK GPU Mapping

| SDL2 Function | PSn00bSDK Equivalent | Notes |
|--------------|---------------------|-------|
| SDL_CreateRGBSurface | GPU framebuffer allocation | Use VRAM directly |
| SDL_BlitSurface | Sprite/polygon primitives | DMA for fast transfers |
| SDL_SetPaletteColors | LoadClut() | PS1 CLUT system |
| SDL_FillRect | FillRect primitive | GPU drawing primitive |
| SDL_RenderPresent | DrawSync/VSync | Swap framebuffers |

### Key PSn00bSDK Graphics Functions (from library reference)

- **InitGraph()** - Initialize graphics subsystem
- **SetDefDrawEnv()** - Set drawing environment
- **SetDefDispEnv()** - Set display environment
- **PutDrawEnv()** - Apply drawing settings
- **PutDispEnv()** - Apply display settings
- **LoadImage()** - DMA transfer to VRAM
- **MoveImage()** - Copy within VRAM
- **DrawSync()** - Wait for drawing completion
- **VSync()** - Wait for vertical blank

## Build System

Create **Makefile.ps1**:
```makefile
# PS1-specific toolchain
CC = mipsel-none-elf-gcc
AR = mipsel-none-elf-ar
LD = mipsel-none-elf-ld

# PSn00bSDK paths
PSN00BSDK = /usr/local/psn00bsdk

# Compiler flags
CFLAGS = -O2 -G0 -Wall -Wpedantic -I$(PSN00BSDK)/include
LDFLAGS = -L$(PSN00BSDK)/lib -lpsxgpu -lpsxcd -lpsxspu -lpsxapi

# Source files (reuse from main project)
SRCS = main.c resource.c uncompress.c ttm.c ads.c story.c walk.c \
       calcpath.c island.c utils.c config.c bench.c \
       graphics_ps1.c sound_ps1.c events_ps1.c

# Output
TARGET = JCREBORN.EXE

# Build rules
$(TARGET): $(SRCS:.c=.o)
	$(LD) -o $@ $^ $(LDFLAGS)
	elf2x $(TARGET) $(TARGET:.EXE=.PS-EXE)

# Create CD image
cd-image: $(TARGET)
	mkpsxiso cd_layout.xml
```

## CD Layout

Create **cd_layout.xml** for mkpsxiso:
```xml
<?xml version="1.0" encoding="UTF-8"?>
<iso_project image_name="jcreborn.bin" cue_sheet="jcreborn.cue">
  <track type="data">
    <directory_tree>
      <file name="JCREBORN.EXE" source="JCREBORN.PS-EXE"/>
      <file name="RESOURCE.MAP" source="jc_resources/RESOURCE.MAP"/>
      <file name="RESOURCE.001" source="jc_resources/RESOURCE.001"/>

      <!-- Sound files -->
      <file name="SOUND0.VAG" source="sounds/sound0.vag"/>
      <!-- ... more sound files ... -->
    </directory_tree>
  </track>
</iso_project>
```

Note: WAV files may need conversion to PS1 VAG format for SPU playback.

## Testing Strategy

1. **Build minimal "hello world" PS1 executable**
   - Test toolchain installation
   - Verify DuckStation can run it

2. **Port graphics layer**
   - Initialize PS1 GPU
   - Display test pattern
   - Verify 640x480 resolution

3. **Port resource loading**
   - Test CD-ROM I/O functions
   - Load and decompress resources from CD
   - Verify LRU cache works on PS1

4. **Port rendering pipeline**
   - Display BMP sprites
   - Render TTM animation
   - Test multi-layer compositing

5. **Port audio**
   - Play test sound effect
   - Implement audio mixing

6. **Port input**
   - Map controller buttons
   - Test hotkeys

7. **Full integration**
   - Run complete story mode
   - Profile memory usage on real hardware
   - Optimize performance

## Optimization Opportunities

- **Texture compression**: Use PS1 4-bit textures where possible
- **DMA transfers**: Leverage hardware DMA for fast VRAM uploads
- **Ordering tables**: Use for efficient rendering without CPU sorting
- **Hardware sprites**: PS1 can handle many sprites in hardware
- **CD streaming**: Stream large resources instead of loading all at once

## Controller Mapping

| PSX Button | Action |
|-----------|--------|
| Start | Pause/Unpause |
| Select | Toggle debug info |
| Triangle | Advance frame (when paused) |
| Circle | Toggle max speed |
| Cross | (reserved) |
| Square | (reserved) |
| L1/R1 | (reserved) |
| L2/R2 | (reserved) |

## Next Steps

1. Install PSn00bSDK toolchain on macOS
2. Install DuckStation emulator
3. Create hello world PS1 program to test toolchain
4. Study PSn00bSDK examples (n00bdemo)
5. Create Makefile.ps1 and cd_layout.xml
6. Begin porting graphics.c to graphics_ps1.c
7. Test incremental builds in emulator

## References

- **PSn00bSDK**: https://github.com/Lameguy64/PSn00bSDK
- **PSn00bSDK Docs**: LibPSn00b Library Reference (libn00bref.pdf)
- **DuckStation**: https://github.com/stenzek/duckstation
- **mkpsxiso**: https://github.com/Lameguy64/mkpsxiso
- **PS1 Dev Resources**: https://psx.arthus.net/
- **Tutorial Series**: http://rsync.irixnet.org/tutorials/pstutorials/

## Success Criteria

- ✅ Builds successfully for PS1 target
- ✅ Runs in DuckStation emulator
- ✅ Memory usage under 2MB system RAM
- ✅ Maintains 30 FPS minimum
- ✅ CD loading times acceptable (<5 seconds)
- ✅ All TTM animations play correctly
- ✅ Story mode progression works
- ✅ Sound effects play via SPU
- ✅ (Stretch) Test on real PS1 hardware

## Current Status

- [x] Created ps1 branch from 4mb2025
- [ ] Install PSn00bSDK toolchain
- [ ] Install DuckStation emulator
- [ ] Create hello world test
- [ ] Port graphics layer
- [ ] Port resource I/O
- [ ] Port audio
- [ ] Port input
- [ ] Full integration test
