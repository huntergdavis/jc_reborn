# PlayStation 1 - Hardware Specifications

Technical specifications of the PlayStation 1 hardware relevant to the Johnny Reborn port.

## CPU and Memory

- **CPU**: MIPS R3000A @ 33.8688 MHz
- **System RAM**: 2MB
- **VRAM**: 1MB (video RAM)
- **SPU RAM**: 512KB (audio RAM)

## Graphics System (GPU)

### Resolution Support
- **Native resolution**: 640x480 interlaced
- **Perfect match** for Johnny Reborn's 640x480 design
- Other modes: 320x240, 256x240, 512x240, 640x240

### Display Modes
- NTSC: 60Hz interlaced
- PAL: 50Hz interlaced
- Both progressive and interlaced modes supported

### VRAM Layout
```
Region             Size    Location
-----------------------------------
Framebuffer 0      300KB   (0, 0) - 640x480x16bit
Framebuffer 1      300KB   (0, 480) - 640x480x16bit
Sprite Cache       ~350KB  (Dynamic allocation)
CLUT Tables        ~50KB   (Color palettes)
-----------------------------------
Total:             1.0MB
```

### Graphics Capabilities
- **Sprites**: Hardware sprite engine
- **Primitives**: Lines, triangles, quads, circles
- **Textures**: 4-bit, 8-bit, 16-bit color depths
- **DMA**: Hardware DMA for fast VRAM transfers
- **Ordering tables**: Hardware-assisted z-sorting
- **CLUT system**: Color lookup tables for indexed color

### Color Modes
- **16-bit RGB**: 5-5-5 format (32,768 colors)
- **8-bit indexed**: 256 colors with CLUT
- **4-bit indexed**: 16 colors with CLUT

## Audio System (SPU)

### SPU Capabilities
- **Hardware voices**: 24 simultaneous channels
- **SPU RAM**: 512KB dedicated audio memory
- **Sample rate**: 44.1 kHz
- **Format**: ADPCM compression (4-bit)
- **Hardware mixing**: All channels mixed in hardware
- **ADSR envelopes**: Hardware envelope generation

### Audio Features
- CD-DA audio playback
- XA-ADPCM streaming audio
- Hardware reverb effects
- Stereo panning per voice

### Sound Effects Budget
```
Sound Effects:   25 files × ~20KB = ~500KB
Reserved:        ~12KB (SPU library overhead)
-----------------------------------
Total:           ~512KB (tight fit!)
```

## Storage (CD-ROM)

### CD-ROM Drive
- **Speed**: 2x (300 KB/s)
- **Capacity**: 650MB (CD-ROM)
- **Format**: ISO 9660 filesystem
- **Access time**: ~150ms average seek

### CD-ROM Features
- Asynchronous reading
- Sector-based access
- CD-DA audio tracks
- XA-ADPCM compressed audio

### I/O Considerations
- **Latency**: 150ms+ for cold seeks
- **Strategy**: Pre-cache critical resources
- **LRU cache**: Essential for good performance

## Input (Controller)

### PSX Controller
- D-pad: 4 directions
- Face buttons: Triangle, Circle, X, Square
- Shoulder buttons: L1, L2, R1, R2
- System buttons: Start, Select

### Controller Features
- Digital input (standard controller)
- Analog input (DualShock)
- Force feedback (DualShock)
- Multi-tap support (up to 4 controllers)

## Memory Constraints

### System RAM (2MB)
```
Code + Data:     ~200 KB  (executable)
Resource Cache:  ~1.5 MB  (LRU cache)
Stack/Heap:      ~300 KB  (runtime)
-----------------------------------------
Total:           ~2.0 MB  (Johnny Reborn fits!)
```

**Johnny Reborn's 350KB peak usage** (from 4mb2025 branch) leaves **1.65MB** of headroom for resource caching.

### VRAM (1MB)
- Framebuffers take 600KB (640x480x16bit doubled)
- ~400KB remaining for sprite/texture cache
- CLUT tables use minimal space
- May be tight with many large sprites

### SPU RAM (512KB)
- 25 sound effects at ~20KB each
- ADPCM compression required
- All sounds must fit in SPU RAM
- Alternative: Stream from CD

## Performance Targets

| Metric | Target | Notes |
|--------|--------|-------|
| Frame Rate | 30 FPS | VSync timing |
| Memory | < 2MB RAM | ✅ 350KB peak |
| VRAM | < 1MB | Careful sprite management |
| Load Time | < 5 sec | CD caching needed |
| SPU RAM | < 512KB | ADPCM compression |

## Hardware Advantages

### Perfect Fit for Johnny Reborn
1. **Native 640x480**: No scaling needed
2. **2MB RAM**: 6x more than peak usage (350KB)
3. **Hardware sprites**: Fast sprite rendering
4. **CLUT system**: Perfect for indexed color sprites
5. **Hardware mixing**: 24 simultaneous sound effects

### Optimization Opportunities
- **DMA transfers**: Fast VRAM uploads
- **Ordering tables**: Hardware z-sorting
- **Hardware sprites**: Many sprites, low CPU cost
- **Texture caching**: Sprites stay in VRAM
- **CD streaming**: Stream large resources

## Technical Limitations

### Challenges
1. **VRAM constraints**: 1MB is tight for many sprites
2. **CD latency**: 150ms+ seeks need caching
3. **SPU RAM**: Must fit all sounds in 512KB
4. **No printf()**: Use visual debugging instead
5. **BSS size**: Keep < 50KB for stability

### Workarounds
- LRU cache from 4mb2025 branch
- Pre-load critical resources
- ADPCM compression for audio
- Visual debugging system (colored screens)
- Malloc large buffers instead of static arrays

## See Also

- [API Mapping](api-mapping.md) - SDL2 → PSn00bSDK translation
- [Build System](build-system.md) - Docker and toolchain
- [Graphics Layer](graphics-layer.md) - GPU implementation
- [Audio Layer](audio-layer.md) - SPU implementation
