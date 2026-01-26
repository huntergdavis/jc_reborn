# PS1 Rendering Debug Skill

Debug PS1 GPU rendering issues using iterative research, testing, and verification in DuckStation emulator.

## Usage

Use this skill when PS1 primitives (triangles, rectangles, sprites) are not rendering as expected. This skill follows a proven iterative debugging workflow that combines research, minimal testing, and systematic verification.

## Debugging Workflow

### Phase 1: Research Working Examples

1. Search for PSn00bSDK examples relevant to the primitive type:
   - POLY_F3 (flat-shaded triangles)
   - POLY_F4 (flat-shaded quads)
   - POLY_G3/G4 (gouraud-shaded)
   - TILE (simple rectangles)
   - SPRT (sprites)

2. Search nolibgs_hello_worlds examples: https://github.com/ABelliqueux/nolibgs_hello_worlds

3. Check PS1 GPU documentation: https://psx-spx.consoledev.net/graphicsprocessingunitgpu/

4. Identify key patterns from examples:
   - Primitive buffer allocation (NOT stack variables)
   - Ordering table usage
   - Rendering order (PutDrawEnv before DrawOTag)
   - Background clearing (isbg flag)

### Phase 2: Create Minimal Test

Create ultra-minimal test file that isolates the issue:

```c
#include <sys/types.h>
#include <psxgpu.h>
#include <psxapi.h>

#define SCREEN_XRES 640
#define SCREEN_YRES 480
#define OTLEN 8
#define BUFFER_SIZE 8192

int main(void) {
    DISPENV disp;
    DRAWENV draw;
    u_long ot[OTLEN];
    char primbuff[BUFFER_SIZE];
    char *nextpri;

    /* Initialize GPU */
    ResetGraph(0);
    SetVideoMode(MODE_NTSC);
    SetDefDispEnv(&disp, 0, 0, SCREEN_XRES, SCREEN_YRES);
    SetDefDrawEnv(&draw, 0, 0, SCREEN_XRES, SCREEN_YRES);

    /* Background clear - distinguishable color for debugging */
    setRGB0(&draw, 0, 0, 64);  /* Dark blue */
    draw.isbg = 1;

    PutDispEnv(&disp);
    PutDrawEnv(&draw);
    ClearOTagR(ot, OTLEN);
    SetDispMask(1);

    /* Render loop */
    while(1) {
        VSync(0);
        ClearOTagR(ot, OTLEN);
        nextpri = primbuff;

        /* Test primitive - POLY_F3 triangle */
        POLY_F3 *tri = (POLY_F3*)nextpri;
        setPolyF3(tri);
        setXY3(tri, 150, 50, 50, 200, 250, 200);
        setRGB0(tri, 255, 0, 0);  /* Red */
        addPrim(ot, tri);
        nextpri += sizeof(POLY_F3);

        /* CRITICAL ORDER */
        PutDrawEnv(&draw);      /* FIRST - clears background */
        DrawOTag(ot+OTLEN-1);   /* SECOND - draws primitives */
    }

    return 0;
}
```

Key principles:
- **Primitive buffer allocation**: `char primbuff[BUFFER_SIZE]` + pointer advancement
- **NOT stack variables**: Primitives must have stable addresses
- **Ordering table usage**: `addPrim(ot, prim)` adds to BASE, `DrawOTag(ot+OTLEN-1)` draws from END
- **Render order**: PutDrawEnv BEFORE DrawOTag (clears THEN draws)

### Phase 3: Build with Clean Rebuild

**CRITICAL**: Make doesn't always detect changes. Always do clean rebuild.

```bash
# 1. Copy source to build directory
sudo cp ps1_test.c build-dir/

# 2. CLEAN rebuild (critical!)
sudo docker run --rm --platform linux/amd64 \
  -v /home/hunter/workspace/jc_reborn:/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project/build-dir && rm -f *.o *.exe *.elf && make"

# 3. Verify executable changed with MD5
md5sum build-dir/ps1_test.exe
```

**Common build issues**:
- Make doesn't detect source changes → force clean rebuild
- Build directory has stale source → copy source explicitly
- Old object files cached → delete .o/.exe/.elf first

### Phase 4: Create Fresh ISO

**CRITICAL**: DuckStation may cache old ISO. Delete first.

```bash
# 1. Delete old ISO completely
rm -f ps1_test.bin ps1_test.cue

# 2. Create fresh ISO
sudo docker run --rm --platform linux/amd64 \
  -v /home/hunter/workspace/jc_reborn:/work -w /work \
  jc-reborn-ps1-dev:amd64 \
  mkpsxiso -y cd_layout.xml

# 3. Fix ownership
sudo chown hunter:hunter ps1_test.*

# 4. Verify ISO created
ls -lh ps1_test.bin ps1_test.cue
```

### Phase 5: Test in DuckStation

1. Launch DuckStation: `flatpak run org.duckstation.DuckStation`
2. Load .cue file (NOT .bin)
3. Observe results:
   - Background color (verifies PutDrawEnv works)
   - Primitive visibility (verifies rendering)
   - Sony logo persistence (means isbg not set)

**Debugging techniques**:
- Use distinguishable background colors (dark blue = RGB 0,0,64)
- Test with oversized primitives first (giant triangle fills screen)
- Disable background clear temporarily (isbg=0) to test if primitives exist
- Use bright primary colors (255,0,0 red) for maximum visibility

### Phase 6: Document Findings

Create markdown document with:
- What works ✅
- What doesn't work ❌
- Critical code patterns
- Build process gotchas
- Verified test cases
- Next investigation steps

## Known Working Patterns

### POLY_F3 Triangles (CONFIRMED WORKING)

```c
POLY_F3 *tri = (POLY_F3*)nextpri;
setPolyF3(tri);
setXY3(tri, x0, y0, x1, y1, x2, y2);
setRGB0(tri, r, g, b);
addPrim(ot, tri);  /* Add to BASE of OT */
nextpri += sizeof(POLY_F3);
```

**Critical details**:
- Must use ordering tables (NOT DrawPrim)
- Must allocate from buffer (NOT stack)
- addPrim(ot, ...) adds to BASE (index 0)
- DrawOTag(ot+OTLEN-1) draws from END

## Known Broken Patterns

### TILE Rectangles (DOES NOT WORK)

```c
TILE *tile = (TILE*)nextpri;
setTile(tile);
setXY0(tile, x, y);
setWH(tile, w, h);
setRGB0(tile, r, g, b);
addPrim(ot, tile);  /* Doesn't render */
```

### DrawPrim() Mixed with Ordering Tables (DOES NOT WORK)

```c
DrawPrim(tile);     /* Doesn't work */
DrawOTag(ot+OTLEN-1);  /* Even when called in different order */
```

### POLY_F4 with setXYWH (DOES NOT WORK)

```c
POLY_F4 *quad = (POLY_F4*)nextpri;
setPolyF4(quad);
setXYWH(quad, x, y, w, h);  /* Doesn't render */
setRGB0(quad, r, g, b);
addPrim(ot, quad);
```

### LoadImage to Texture Area - SOLVED (December 2025)

**Problem**: Calling `LoadImage()` with a pointer through a struct member (e.g., `surface->pixels`) breaks OT primitive rendering. This is NOT about the VRAM destination coordinates - it's about how the source buffer is passed to LoadImage.

**Root cause discovered**: LoadImage DMA seems to have issues when the source pointer comes from struct member access. Possibly a compiler optimization or pointer aliasing issue with PSn00bSDK.

**What BREAKS OT**:
```c
/* BROKEN - Using surface->pixels directly in LoadImage */
PS1Surface *surface = safe_malloc(sizeof(PS1Surface));
surface->pixels = safe_malloc(pixelSize);
memcpy(surface->pixels, srcData, pixelSize);
LoadImage(&rect, (uint32*)surface->pixels);  /* <-- BREAKS OT */
```

**What WORKS**:
```c
/* WORKING - Use simple pointer, create struct AFTER LoadImage */
uint16 *copyBuf = safe_malloc(pixelSize);
memcpy(copyBuf, srcData, pixelSize);
LoadImage(&rect, (uint32*)copyBuf);  /* <-- WORKS */
DrawSync(0);

/* Create struct AFTER LoadImage completed */
PS1Surface *surface = safe_malloc(sizeof(PS1Surface));
surface->pixels = copyBuf;
/* ... rest of surface setup ... */
```

**Critical pattern for grLoadBmp**:
1. Allocate pixel buffer with simple `uint16*` variable
2. Copy BMP data to the buffer
3. Call LoadImage with the simple pointer
4. Call DrawSync(0)
5. THEN create PS1Surface struct and store the pointer
6. Cap sprite dimensions to 64x64 maximum

**Additional requirement**:
- Sprite dimensions must be capped at 64x64 to prevent OT rendering issues
- Large texture uploads may still cause problems even with correct pointer pattern

## Workarounds

### Drawing Rectangles

Use two triangles instead of TILE or POLY_F4:

```c
/* Rectangle as two triangles */
/* Top-left triangle */
POLY_F3 *tri1 = (POLY_F3*)nextpri;
setPolyF3(tri1);
setXY3(tri1, x, y, x+w, y, x, y+h);
setRGB0(tri1, r, g, b);
addPrim(ot, tri1);
nextpri += sizeof(POLY_F3);

/* Bottom-right triangle */
POLY_F3 *tri2 = (POLY_F3*)nextpri;
setPolyF3(tri2);
setXY3(tri2, x+w, y, x+w, y+h, x, y+h);
setRGB0(tri2, r, g, b);
addPrim(ot, tri2);
nextpri += sizeof(POLY_F3);
```

## PROVEN WORKING PATTERN (December 2025)

**Critical insight**: After loadTitleScreenEarly() and parseResourceFiles(), a FRESH `ResetGraph(0)` is required to get clean GPU state. Without this, OT primitives don't render.

### Working Initialization Sequence

```c
/* 1. Show title screen FIRST - instant visual feedback */
loadTitleScreenEarly();

/* 2. Parse resource files from CD */
parseResourceFiles("RESOURCE.MAP");
initLRUCache();

/* 3. FRESH GPU RESET - Critical! */
ResetGraph(0);
SetVideoMode(MODE_NTSC);
InitGeom();

/* 4. Local OT and primitive buffer (NOT module's) */
#define GAME_OTLEN 8
#define GAME_PRIMBUF 8192
static unsigned long gameOT[GAME_OTLEN];
static char gamePrimBuf[GAME_PRIMBUF];
char *gameNextPri;

/* 5. Display/Draw environments */
DISPENV gameDisp;
DRAWENV gameDraw;
SetDefDispEnv(&gameDisp, 0, 0, 640, 480);
SetDefDrawEnv(&gameDraw, 0, 0, 640, 480);
gameDisp.isinter = 1;  /* Interlaced for 640x480 */
gameDraw.isbg = 0;     /* Don't clear - grDrawBackground handles it */
SetDispMask(1);
PutDispEnv(&gameDisp);
PutDrawEnv(&gameDraw);

/* 6. Load palette and background */
grLoadPalette(palResource);
grLoadScreen(scrResource->resName);
```

### Working Main Loop

```c
while (1) {
    DrawSync(0);      /* Wait for previous frame's GPU ops */
    VSync(0);         /* Wait for vsync */
    ClearOTagR(gameOT, GAME_OTLEN);
    gameNextPri = gamePrimBuf;

    /* Upload background via LoadImage (has DrawSync at end) */
    grDrawBackground();

    /* Add primitives to OT */
    POLY_F3 *sprite = (POLY_F3*)gameNextPri;
    setPolyF3(sprite);
    setXY3(sprite, x1, y1, x2, y2, x3, y3);
    setRGB0(sprite, r, g, b);
    addPrim(&gameOT[0], sprite);
    gameNextPri += sizeof(POLY_F3);

    /* Draw OT */
    PutDrawEnv(&gameDraw);
    DrawOTag(gameOT + GAME_OTLEN - 1);
}
```

### Key Points

1. **Fresh ResetGraph(0) AFTER all initialization** - loadTitleScreenEarly and parseResourceFiles corrupt GPU state
2. **Use local OT (gameOT), not module's OT (ot[db])** - grDrawSprite uses module OT which conflicts
3. **grDrawBackground uses LoadImage + DrawSync** - background uploaded directly to framebuffer
4. **isbg=0** - Don't use background clearing; grDrawBackground handles it
5. **Order: grDrawBackground THEN add primitives THEN DrawOTag** - primitives render ON TOP of background

## Success Criteria

- Test executable boots in DuckStation
- Background clears to expected color each frame
- Primitives render with correct colors and positions
- No Sony logo persistence (isbg=1 working)
- Build process produces different MD5 after code changes
- Findings documented for future reference

### Texture Page Boundary Alignment - SOLVED (December 2025)

**Problem**: Sprite animation frames appear to wrap around - part of the sprite shows on the wrong side, causing a "jerky" effect where feet appear in front of the character.

**Root cause**: For 4-bit textures, each texture page is 64 VRAM pixels wide (256 texture pixels). UV coordinates are 8-bit (0-255). If a sprite's UV offset plus width exceeds 256, the right portion wraps to the left side of the texture page.

**Example of failure**:
- Sprite stored at VRAM X=696, width=64 texture pixels
- UV offset = (696 % 64) * 4 = 56 * 4 = 224
- UV offset + width = 224 + 64 = 288 > 256
- Result: Right 32 pixels wrap to left side!

**Solution**: When allocating VRAM for sprites, check if the sprite would cross a texture page boundary. If so, align to the next page:

```c
/* Check texture page boundary before allocating */
uint16 vramW = spriteWidth / 4;  /* 4-bit: VRAM width = texture pixels / 4 */
uint16 pageStart = (nextVRAMX / 64) * 64;  /* Current texture page start */
uint16 pageEnd = pageStart + 64;           /* Current texture page end */

if (nextVRAMX + vramW > pageEnd) {
    /* Would cross page boundary - align to next page */
    nextVRAMX = pageEnd;
    if (nextVRAMX >= 1024) {
        nextVRAMX = 640;
        nextVRAMY += MAX_SPRITE_DIM;  /* Move down */
    }
}
```

**Additional fix**: When capping sprite dimensions (e.g., 64x64 max), copy row-by-row with proper stride, not contiguous bytes:

```c
if (width != safeW || height != safeH) {
    /* Row-by-row copy to handle different source/dest strides */
    uint8 *dst = (uint8*)copyBuf;
    uint8 *src = srcPtr;
    uint32 srcRowBytes = width / 2;   /* 4-bit = 2 pixels per byte */
    uint32 dstRowBytes = safeW / 2;
    for (uint16 y = 0; y < safeH; y++) {
        memcpy(dst, src, dstRowBytes);
        dst += dstRowBytes;
        src += srcRowBytes;
    }
}
```

### Sprite Animation Alignment - SOLVED (December 2025)

**Problem**: Walking animation frames have different sizes, causing the sprite to jump around as frames change.

**Solution**: Center sprites horizontally and bottom-align vertically so feet stay in place:

```c
/* Base position is where feet should be */
int baseX = 350;  /* Center X position */
int baseY = 364;  /* Bottom Y position (feet on ground) */

/* Calculate draw position: center horizontally, bottom-align vertically */
int spriteX = baseX - (sprite->width / 2);
int spriteY = baseY - sprite->height;
```

### TTM Animation Issues - DIAGNOSED (January 2026)

**Issue 1: DRAW_SPRITE_FLIP showing wrong direction**

**Symptoms**: Johnny appears to face backwards for certain frame ranges (e.g., frames 7-12). The DRAW_SPRITE_FLIP opcode (0xA524) is not producing correct results.

**Root cause investigation needed**:
- Check if `grCompositeToBackgroundFlip()` is being called at all
- Verify horizontal flip logic in pixel copying loop
- The flip should mirror the sprite horizontally when compositing to background tiles

**Relevant code** (`graphics_ps1.c`):
```c
void grDrawSpriteFlip(PS1Surface *sfc, struct TTtmSlot *ttmSlot, sint16 x, sint16 y,
                      uint16 spriteNo, uint16 imageNo)
{
    // ... validation ...

    /* RAM-based sprites use composite with flip */
    if (sprite->x == 0 && sprite->y == 0 && sprite->pixels != NULL) {
        grCompositeToBackgroundFlip(sprite, x, y);  // <-- Is this working?
        return;
    }
    // ... VRAM path (not used for RAM sprites) ...
}
```

**Issue 2: Frame modulo wrap causing wrong sprites**

**Symptoms**: Extra Johnny sprites appear at wrong positions (e.g., fishing pose at bottom when he should be walking). Animation has 40+ frames but only 8 are loaded due to memory constraints.

**Root cause**: Frame index wrapping arithmetic:
```c
/* This wraps frame 18 to frame 2, showing wrong sprite! */
uint16 actualSpriteNo = spriteNo % ttmSlot->numSprites[imageNo];
```

When animation requests frame 18 but only 8 frames loaded:
- Frame 18 % 8 = 2 → Shows frame 2 instead of frame 18
- Different frames have different poses/positions, causing "ghost" sprites

**Solutions needed**:
1. **Short-term**: Accept limited animation with only 8 frames (may look choppy)
2. **Long-term**: Implement frame streaming/recycling to load frames on-demand

**Frame streaming approach**:
- Keep a small cache (4-8 frames) in RAM
- When animation requests frame N, check if loaded
- If not loaded, evict oldest frame and load N from CD
- Requires async CD read to avoid stalling animation

### Memory Constraints - PS1 Sprite Loading (January 2026)

**Problem**: PS1 has ~2MB RAM total. Loading all sprite frames exhausts memory.

**JOHNWALK.BMP analysis**:
- 42 frames of walking animation
- Each frame ~50x80 pixels @ 16-bit = ~8KB per frame
- Total: 42 × 8KB = ~336KB just for one character animation

**Memory budget breakdown**:
- Background tiles (4 × 320×240 @ 16-bit): ~600KB working + ~600KB clean backup
- Executable code: ~60KB
- CD sector buffer: ~64KB per read
- Sprite frames: MAX_SPRITES_PER_BMP × ~8KB per BMP slot

**Current limit**: MAX_SPRITES_PER_BMP = 8 (reduced from 120 → 16 → 8)

**Impact**: Animations cycle through only first 8 frames, causing:
- Incorrect walk cycles (wrong feet positions)
- Fishing frames appearing during walk (frame 18 → frame 2)
- Animation looks "jumpy" instead of smooth

## Resources

- PSn00bSDK examples: https://github.com/Lameguy64/PSn00bSDK/tree/master/examples
- nolibgs examples: https://github.com/ABelliqueux/nolibgs_hello_worlds
- PS1 GPU specs: https://psx-spx.consoledev.net/graphicsprocessingunitgpu/
- Project findings: PS1_TRIANGLE_FINDINGS.md in repository root
