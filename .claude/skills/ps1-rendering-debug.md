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

## Resources

- PSn00bSDK examples: https://github.com/Lameguy64/PSn00bSDK/tree/master/examples
- nolibgs examples: https://github.com/ABelliqueux/nolibgs_hello_worlds
- PS1 GPU specs: https://psx-spx.consoledev.net/graphicsprocessingunitgpu/
- Project findings: PS1_TRIANGLE_FINDINGS.md in repository root
