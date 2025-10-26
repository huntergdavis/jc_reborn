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
