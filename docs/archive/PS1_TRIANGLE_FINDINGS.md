# PS1 Triangle Rendering - Key Findings

## What Works ✅

### POLY_F3 Triangles
**Triangles render successfully** when using this approach:

```c
// Setup primitive buffer and ordering table
char primbuff[8192];
u_long ot[8];
char *nextpri;

// In render loop:
ClearOTagR(ot, 8);
nextpri = primbuff;

// Allocate triangle from buffer (NOT stack variable!)
POLY_F3 *tri = (POLY_F3*)nextpri;
setPolyF3(tri);
setXY3(tri, x0, y0, x1, y1, x2, y2);
setRGB0(tri, r, g, b);
addPrim(ot, tri);  // Add to BASE of OT
nextpri += sizeof(POLY_F3);

// Render
PutDrawEnv(&draw);  // FIRST - clears background
DrawOTag(ot+OTLEN-1);  // SECOND - draws primitives
```

**Critical ordering table usage:**
- `addPrim(ot, prim)` - adds to BASE/start of ordering table
- `DrawOTag(ot+OTLEN-1)` - draws from END, traversing backwards

## What Doesn't Work ❌

### TILE Rectangles
- `TILE` primitives don't render when added to ordering tables
- `DrawPrim()` doesn't work when mixed with `DrawOTag()` rendering
- Even calling `DrawPrim()` before or after ordering table doesn't work

### POLY_F4 Quads
- `POLY_F4` with `setXYWH()` doesn't render (tested)
- May need manual vertex setup instead of setXYWH

## Build Process Issues 🔨

### Critical Steps for Rebuild:
```bash
# 1. Copy source to build directory
sudo cp ps1_ultra_minimal.c build-ultra/

# 2. CLEAN rebuild (critical!)
sudo docker run --rm --platform linux/amd64 \
  -v /home/hunter/workspace/jc_reborn:/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project/build-ultra && rm -f *.o *.exe *.elf && make"

# 3. Delete old ISO completely
rm -f ps1_ultra.bin ps1_ultra.cue

# 4. Create fresh ISO
sudo docker run --rm --platform linux/amd64 \
  -v /home/hunter/workspace/jc_reborn:/work -w /work \
  jc-reborn-ps1-dev:amd64 \
  mkpsxiso -y cd_layout_ultra.xml

# 5. Fix ownership
sudo chown hunter:hunter ps1_ultra.*

# 6. Verify with MD5
md5sum build-ultra/ps1_ultra.exe
```

**Common Issues:**
- Make doesn't always detect source changes → **force clean rebuild**
- Old ISO gets cached by DuckStation → **delete ISO files first**
- Docker creates files as root → **chown after ISO creation**
- Build directory has stale source → **copy source explicitly**

## PS1 Rendering Pipeline 🎨

### Correct Order:
1. `VSync(0)` - wait for vertical blank
2. `ClearOTagR(ot, OTLEN)` - clear ordering table
3. Reset primitive buffer pointer: `nextpri = primbuff`
4. Allocate and setup primitives from buffer
5. Add primitives to ordering table: `addPrim(ot, prim)`
6. **`PutDrawEnv(&draw)`** - FIRST (clears background if isbg=1)
7. **`DrawOTag(ot+OTLEN-1)`** - SECOND (draws primitives)

### Background Clearing:
```c
setRGB0(&draw, r, g, b);
draw.isbg = 1;  // Enable automatic background clear
```

## Verified Working Test 🎯

**Current working demo:**
- Dark blue background (cleared each frame)
- Red triangle (top left)
- Green triangle (top center)
- Blue triangle (top right)

**Test file:** `ps1_ultra_minimal.c` in `/home/hunter/workspace/jc_reborn/`

## Next Steps 📋

1. ✅ Triangles work with ordering tables
2. ❌ Need to solve rectangle rendering
3. 🔄 Investigate POLY_F4 manual vertex setup
4. 🔄 Consider using only triangles (2 triangles = rectangle)
5. 📝 Document as reusable Claude skill

## Resources 📚

- **PSn00bSDK examples:** https://github.com/Lameguy64/PSn00bSDK/tree/master/examples
- **nolibgs examples:** https://github.com/ABelliqueux/nolibgs_hello_worlds
- **PS1 GPU specs:** https://psx-spx.consoledev.net/graphicsprocessingunitgpu/
