# PS1 Port - API Mapping Reference

This document shows how SDL2 functions are mapped to PSn00bSDK equivalents for the PlayStation 1 port.

## Graphics API Mapping

| SDL2 Function | PSn00bSDK Equivalent | Status | Notes |
|--------------|---------------------|--------|-------|
| `SDL_Init(SDL_INIT_VIDEO)` | `ResetGraph(0)`, `InitGeom()` | ✅ Done | Reset GPU and GTE |
| `SDL_CreateWindow()` | `SetDefDispEnv()` | ✅ Done | 640x480 interlaced |
| `SDL_CreateRGBSurface()` | `PS1Surface` struct | ✅ Done | Custom surface type |
| `SDL_BlitSurface()` | `SPRT` primitives | ✅ Done | Hardware sprites |
| `SDL_SetPaletteColors()` | `LoadClut()` | ✅ Done | CLUT upload to VRAM |
| `SDL_FillRect()` | `TILE` primitive | ✅ Done | GPU rectangle primitive |
| `SDL_UpdateWindowSurface()` | `PutDispEnv()`, `VSync()` | ✅ Done | Buffer swap |
| `SDL_RenderPresent()` | `DrawOTag()`, `VSync()` | ✅ Done | Display ordering table |

### Detailed Graphics Mappings

#### Initialization
```c
// SDL2
SDL_Init(SDL_INIT_VIDEO);
SDL_Window *window = SDL_CreateWindow(...);

// PSn00bSDK
ResetGraph(0);              // Reset GPU
SetVideoMode(MODE_NTSC);    // Set video mode
InitGeom();                 // Initialize GTE
SetDefDispEnv(&disp, 0, 0, 640, 480);
SetDefDrawEnv(&draw, 0, 0, 640, 480);
```

#### Surface Creation
```c
// SDL2
SDL_Surface *surface = SDL_CreateRGBSurface(0, w, h, 8, ...);

// PSn00bSDK
PS1Surface *surface = malloc(sizeof(PS1Surface));
surface->width = w;
surface->height = h;
surface->vramX = nextVRAMX;  // Allocate VRAM position
surface->vramY = nextVRAMY;
surface->pixels = malloc(w * h);
```

#### Sprite Blitting
```c
// SDL2
SDL_BlitSurface(sprite, &srcRect, screen, &dstRect);

// PSn00bSDK
SPRT *sprt = (SPRT*)nextPrimitive;
setSprt(sprt);
setXY0(sprt, x, y);
setWH(sprt, w, h);
setUV0(sprt, u, v);
setRGB0(sprt, 128, 128, 128);
addPrim(ot, sprt);
```

#### Display Update
```c
// SDL2
SDL_UpdateWindowSurface(window);

// PSn00bSDK
DrawSync(0);        // Wait for GPU
VSync(0);           // Wait for vblank
PutDrawEnv(&draw);  // Set draw environment
DrawOTag(ot);       // Draw ordering table
```

## Input API Mapping

| SDL2 Function | PSn00bSDK Equivalent | Status | Notes |
|--------------|---------------------|--------|-------|
| `SDL_PollEvent()` | `PAD library` | ✅ Done | Controller polling |
| `SDL_KEYDOWN` | Button press check | ✅ Done | Direct button state |
| `SDL_GetTicks()` | `VSync` counter | ✅ Done | Frame-based timing |

### Detailed Input Mappings

#### Initialization
```c
// SDL2
SDL_Init(SDL_INIT_EVENTS);

// PSn00bSDK
InitPAD(pad_buff[0], 34, pad_buff[1], 34);
StartPAD();
ChangeClearPAD(0);
```

#### Event Polling
```c
// SDL2
SDL_Event event;
while (SDL_PollEvent(&event)) {
    if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_ESCAPE) quit = 1;
    }
}

// PSn00bSDK
PADTYPE *pad = (PADTYPE*)pad_buff[0];
if (pad->stat == 0) {  // Controller connected
    if (!(pad->btn & PAD_SELECT)) quit = 1;
    if (!(pad->btn & PAD_START)) pause = !pause;
}
```

#### Timing
```c
// SDL2
uint32 now = SDL_GetTicks();

// PSn00bSDK
static int frameTick = 0;
VSync(0);  // Wait for frame
frameTick++;
```

## Audio API Mapping

| SDL2 Function | PSn00bSDK Equivalent | Status | Notes |
|--------------|---------------------|--------|-------|
| `Mix_Init()` | `SpuInit()` | ✅ Done | Initialize SPU |
| `Mix_LoadWAV()` | VAG load + transfer | ⏳ TODO | ADPCM conversion needed |
| `Mix_PlayChannel()` | `SpuSetKey()` | ⏳ TODO | Voice allocation |
| `Mix_Volume()` | `SpuSetVoiceAttr()` | ⏳ TODO | Volume control |

### Detailed Audio Mappings

#### Initialization
```c
// SDL2
Mix_Init(MIX_INIT_WAV);
Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 2048);

// PSn00bSDK
SpuInit();
SpuSetMute(SPU_OFF);
SpuSetCommonMasterVolume(0x3FFF, 0x3FFF);
```

#### Sound Loading (TODO)
```c
// SDL2
Mix_Chunk *sound = Mix_LoadWAV("sound.wav");

// PSn00bSDK (needs implementation)
// 1. Convert WAV to VAG format (ADPCM)
// 2. Load VAG data into buffer
// 3. Transfer to SPU RAM
SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
SpuSetTransferStartAddr(spu_addr);
SpuWrite(vag_data, vag_size);
SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
```

#### Sound Playback (TODO)
```c
// SDL2
Mix_PlayChannel(-1, sound, 0);

// PSn00bSDK (needs implementation)
SpuSetVoiceAttr(voice, ...);  // Set voice parameters
SpuSetKey(SPU_ON, voice);     // Start playback
```

## File I/O API Mapping

| Standard C | PSn00bSDK Equivalent | Status | Notes |
|-----------|---------------------|--------|-------|
| `fopen()` | `CdSearchFile()` | ⏳ TODO | Locate file on CD |
| `fread()` | `CdRead()` | ⏳ TODO | Read sectors |
| `fseek()` | Sector calculation | ⏳ TODO | Manual positioning |
| `fclose()` | N/A | ⏳ TODO | No handles needed |

### Detailed File I/O Mappings (TODO)

#### File Opening
```c
// Standard C
FILE *f = fopen("RESOURCE.MAP", "rb");

// PSn00bSDK (needs implementation)
CdlFILE file;
if (CdSearchFile(&file, "\\RESOURCE.MAP;1") == NULL) {
    // File not found
}
// Store file position for later reads
```

#### File Reading
```c
// Standard C
fread(buffer, 1, size, f);

// PSn00bSDK (needs implementation)
// Calculate sector and offset
int sector = file.pos.minute * 60 * 75 + 
             file.pos.second * 75 + 
             file.pos.sector;
CdControlB(CdlSetloc, (u_char*)&file.pos, 0);
CdRead(num_sectors, (u_long*)buffer, CdlModeSpeed);
CdReadSync(0, 0);  // Wait for completion
```

## Key Differences

### Surface Management
- **SDL2**: Software surfaces with direct pixel access
- **PS1**: Hardware surfaces in VRAM, accessed via DMA

### Rendering Model
- **SDL2**: Immediate mode (draw, flip)
- **PS1**: Ordering tables (queue primitives, submit)

### Color Format
- **SDL2**: 8-bit indexed or 24/32-bit RGBA
- **PS1**: 16-bit RGB555 or 8/4-bit indexed with CLUT

### Timing
- **SDL2**: Millisecond timer (`SDL_GetTicks()`)
- **PS1**: Frame-based (`VSync()` counter)

### File I/O
- **SDL2**: Standard C `stdio` (buffered)
- **PS1**: CD-ROM sectors (asynchronous, unbuffered)

## PSn00bSDK Key Functions

### Graphics (`psxgpu.h`)
- `ResetGraph(mode)` - Reset GPU subsystem
- `SetDefDispEnv(env, x, y, w, h)` - Configure display environment
- `SetDefDrawEnv(env, x, y, w, h)` - Configure drawing environment
- `PutDispEnv(env)` - Apply display environment
- `PutDrawEnv(env)` - Apply drawing environment
- `LoadImage(rect, data)` - DMA transfer to VRAM
- `MoveImage(src, dst)` - Copy within VRAM
- `DrawSync(mode)` - Wait for GPU drawing completion
- `VSync(mode)` - Wait for vertical blank
- `ClearOTagR(ot, len)` - Clear ordering table (reverse order)
- `addPrim(ot, prim)` - Add primitive to ordering table
- `DrawOTag(ot)` - Process ordering table

### Audio (`psxspu.h`)
- `SpuInit()` - Initialize SPU
- `SpuQuit()` - Shutdown SPU
- `SpuSetMute(mode)` - Mute/unmute SPU
- `SpuSetCommonMasterVolume(left, right)` - Set master volume
- `SpuSetVoiceAttr(voice, attr)` - Configure voice parameters
- `SpuSetKey(on_off, voice_bit)` - Start/stop voice playback
- `SpuSetTransferMode(mode)` - Set DMA transfer mode
- `SpuSetTransferStartAddr(addr)` - Set SPU RAM address
- `SpuWrite(data, size)` - Write data to SPU RAM

### Input (`psxpad.h`)
- `InitPAD(buf1, len1, buf2, len2)` - Initialize controller buffers
- `StartPAD()` - Begin controller polling
- `ChangeClearPAD(mode)` - Configure V-Blank acknowledgment
- Button constants: `PAD_SELECT`, `PAD_START`, `PAD_UP`, `PAD_DOWN`, etc.

### CD-ROM (`psxcd.h`)
- `CdInit()` - Initialize CD-ROM (DON'T CALL when booting from CD!)
- `CdSearchFile(file, name)` - Locate file in ISO9660 filesystem
- `CdControlB(command, param, result)` - Send CD-ROM command
- `CdRead(sectors, dest, mode)` - Read sectors asynchronously
- `CdReadSync(mode, result)` - Wait for read completion

## See Also

- [Hardware Specs](hardware-specs.md) - PS1 technical specifications
- [Graphics Layer](graphics-layer.md) - Detailed GPU implementation
- [Audio Layer](audio-layer.md) - Detailed SPU implementation
- [Input Layer](input-layer.md) - Detailed controller implementation
