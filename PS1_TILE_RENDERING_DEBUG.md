# PS1 Sprite Tiling Debug Log

## Problem Statement
JOHNWALK.BMP sprites are 64x78 pixels - 14 pixels taller than the PS1's 64-pixel texture limit. Johnny's feet are cut off because only the top 64 rows are loaded/rendered.

---

## CURRENT STATUS (2025-12-27)

### What Works
| Frames | Multi-Tile | Result |
|--------|------------|--------|
| 1 frame | YES | Johnny shows feet correctly |
| 2 frames | YES | Both frames render with correct feet |

### What's Broken
| Frames | Multi-Tile | Result |
|--------|------------|--------|
| 3 frames | YES | **FULL REGRESSION** - NO sprites render (including island) |
| 6 frames | YES | **FULL REGRESSION** - NO sprites render |
| 42 frames | YES | **FULL REGRESSION** - NO sprites render |

### The Breakpoint
**Exactly at frame 3** (0-indexed: frame 2)
- 2 frames = WORKS
- 3 frames = COMPLETELY BROKEN

### Current Workaround
Multi-tile BMPs limited to 2 frames in `graphics_ps1.c:575-578`:
```c
/* For multi-tile BMPs, limit to 2 frames (3+ breaks completely) */
if (needsMultiTile && numToLoad > 2) {
    numToLoad = 2;
}
```

### Key Fix Applied
**addPrim ordering** in `grDrawSpriteExt` (line ~1247):
```c
/* Add sprt FIRST so tpage renders BEFORE it
 * (addPrim adds to HEAD, so last added = first rendered) */
addPrim(extOT, sprt);   // renders SECOND
addPrim(extOT, tpage);  // renders FIRST (sets texture page)
```

### Suspected Causes for Frame 3 Failure
1. **srcPtr corruption** - pointer calculation may go wrong after loading 2 frames + 2 bottom tiles
2. **VRAM collision** - frame 2's bottom tile may overwrite critical texture data
3. **Memory exhaustion** - third bottom tile malloc may fail silently
4. **Off-by-one error** - loop indexing bug manifesting at frame 3

### Next Steps
1. Add debug output to track srcPtr values during frame loading
2. Check VRAM placement of each bottom tile
3. Verify malloc returns valid pointer for frame 3
4. Trace exactly where the failure occurs

---

## Technical Constraints

### PS1 Hardware Limits
- **MAX_SPRITE_DIM = 64**: UV coordinates are 8-bit (0-255), and for 4-bit textures, each texture page is 64 VRAM pixels wide = 256 texture pixels
- **Texture page**: 64 VRAM pixels wide x 256 VRAM pixels tall
- **VRAM layout**: Framebuffer at (0,0)-(639,479), CLUTs at (640,0-3), textures at (640,4+)

### Sprite Data
- JOHNWALK.BMP: 64x78 pixels per frame, 42 frames
- BACKGRND.BMP (island): 100+ pixel tall sprites, loaded via RAM (no 64-pixel limit)

## Multi-Tile Infrastructure (Already Exists)
- PS1Surface has: fullWidth, fullHeight, tileOffsetX, tileOffsetY, nextTile
- grDrawSprite() walks linked list with tile offsets
- grDrawSpriteFlip() walks linked list
- grFreeLayer() walks linked list to free all tiles

## Trials and Results

### Trial 1: Initial multi-tile loading attempt (earlier session)
**What**: Added bottom tile loading in grLoadBmp after main tile
**Result**: BROKE ALL SPRITE RENDERING
**Hypothesis**: Texture page or UV calculation issue

### Trial 2: Texture page sharing fix
**What**: Calculate tpage ONCE from first tile, use for all tiles
**Code location**: grDrawSprite ~line 1056
**Result**: Unknown - need to test in isolation

### Trial 3: Debug tracking variables
**What**: Added grDebugMaxSpriteHeight, grDebugMultiTileCount
**Result**: Showed JOHNWALK max height ~78 pixels (confirmed need for multi-tile)

### Trial 4: Visual debug bars
**What**: Drew colored bars to show max height vs 64-pixel threshold
**Result**: Confirmed sprites exceed 64 pixels

### Trial 5: Debug halt to show dimensions
**What**: Halted game to display "width=64 height=78 fullW=64 fullH=78"
**Result**: CONFIRMED JOHNWALK frame 0 is 64x78 pixels

### Trial 6: LoadImage timing test
**What**: Tried LoadImage in main loop vs during init
**Observation**: User reported seeing hat repeated below Johnny with red tint
**Interpretation**: Bottom tile WAS rendering, but showing wrong VRAM content

### Trial 7: LoadImage with malloc vs safe_malloc
**What**: Tested if buffer allocation type matters
**Result**: User reported seeing white rectangle when using malloc in main loop
**BUT**: User later said this was hallucinated - no actual change observed

### Trial 8: Current attempt (BROKEN)
**What**:
1. Added bottom tile loading in grLoadBmp (lines 648-697)
2. Fixed grDrawSpriteExt to walk linked list (lines 1192-1238)
3. Fixed positioning to use fullHeight
**Result**: NO SPRITES RENDERING AT ALL

## Current Code State (after revert + new changes)

### grLoadBmp changes (graphics_ps1.c ~line 648):
```c
/* Multi-tile: Load bottom portion if sprite is taller than 64 pixels */
if (height > MAX_SPRITE_DIM) {
    uint16 bottomH = height - MAX_SPRITE_DIM;
    // ... allocate buffer, copy rows 64+, LoadImage, create PS1Surface, link
}
```

### grDrawSpriteExt changes (graphics_ps1.c ~line 1192):
```c
/* Walk linked list of tiles */
PS1Surface *tile = sprite;
while (tile != NULL) {
    // ... draw each tile with tileOffsetX/Y applied
    tile = tile->nextTile;
}
```

### jc_reborn.c positioning:
```c
int spriteX = baseX - (loadedSprite->fullWidth / 2);
int spriteY = baseY - loadedSprite->fullHeight;
```

## Known Issues to Investigate

1. **Why does adding bottom tile loading break ALL rendering?**
   - Could be VRAM corruption
   - Could be primitive buffer overflow
   - Could be texture page conflict

2. **UV coordinate calculation for bottom tile**
   - Main tile at (vramX, vramY): UV = (0, vramY % 256)
   - Bottom tile at (vramX, vramY+64): UV = (0, (vramY+64) % 256)
   - If vramY=4, bottom UV_V = 68 - should be correct

3. **Texture page for bottom tile**
   - Main tile tpage: (vramX/64, vramY/256) = (10, 0)
   - Bottom tile tpage: (vramX/64, (vramY+64)/256) = (10, 0) - SAME, good

## Next Steps to Try

1. **Minimal test**: Load ONLY frame 0, ONLY main tile (no bottom), verify it works
2. **Add bottom tile for frame 0 ONLY**, verify main tile still works
3. **Check primitive buffer size** - are we overflowing?
4. **Check VRAM space** - are bottom tiles overwriting other data?

## Questions for Debugging

- Does the main tile render if we DON'T add bottom tile code? **YES - baseline works**
- Does adding just the PS1Surface allocation (without LoadImage) break things?
- Does adding just the LoadImage (without linking) break things?
- Is the primitive buffer large enough for extra tiles?

---

## Trial 9: Incremental approach - linked list walking ONLY

**Date**: Current session
**Goal**: Add linked list walking to grDrawSpriteExt WITHOUT adding bottom tiles yet
**Hypothesis**: Since nextTile is NULL in baseline, this change should be safe and not break anything

### Step 9a: Modify grDrawSpriteExt to walk linked list
**Code change**: Wrap rendering in while loop, add tileOffsetX/Y to screen position
**Expected result**: Should still work identically (nextTile is NULL)
**Actual result**: SUCCESS - no regression, Johnny renders same as before

### Step 9b: Add bottom tile loading in grLoadBmp
**Code change**: After main tile, if height > 64, create bottom tile and link it
**Key details**:
- Bottom tile placed at same vramX, vramY + 64 (stacked vertically in same texture page)
- Copy rows 64+ from source BMP with nibble swap
- Link: surface->nextTile = bottomTile
**Expected result**: Johnny's feet should appear (full 78 pixel height)
**Actual result**: FULL REGRESSION - NO SPRITES RENDERED AT ALL
**Reverted**: Yes

### Step 9c: Isolate the issue - LoadImage WITHOUT linking
**Goal**: Determine if VRAM loading or linking causes the regression
**Code change**: Call LoadImage for bottom tile, but leave nextTile = NULL
**Expected result**: If sprites render (feet still cut off), then VRAM loading is safe
**Actual result**: FULL REGRESSION - NO SPRITES. LoadImage itself is the problem!
**Reverted**: Yes

## CRITICAL FINDING: VRAM Layout Collision!

**Root cause discovered**: Bottom tiles OVERWRITE the next row of main tiles!

**Current VRAM layout**:
```
Row 1 (Y=4):    [Frame0][Frame1][Frame2]...[Frame N] ← main tiles
                  ↓       ↓       ↓
Row 1 bottom:   Y=68: bottom tiles placed here

Row 2 (Y=68):   [FrameN+1][FrameN+2]... ← OVERWRITES bottom tiles!
```

**The bug**:
1. Main tiles laid out horizontally at nextVRAMY (starts at 4)
2. Bottom tiles placed at nextVRAMY + 64 (= 68)
3. When X wraps, nextVRAMY += 64, so next row starts at Y=68
4. Second row of main tiles OVERWRITES first row's bottom tiles!

### Step 9d: Fix VRAM layout - reserve space for multi-tile sprites
**Plan**: Track max sprite height per row, advance Y by max height (not fixed 64)
**Code changes**:
- Added `rowMaxHeight` tracking variable
- Changed row wrap to use `nextVRAMY += rowMaxHeight` instead of `+= 64`
**Expected result**: Baseline still works, bottom tiles won't be overwritten
**Actual result**: PARTIAL REGRESSION - Johnny split: half at bottom, legs at top
**Analysis**: UV or positioning is now wrong due to different VRAM Y positions
**Note**: This is WITHOUT bottom tile loading - just the VRAM layout change
**Reverted**: YES - back to baseline

### Step 9e: Simpler approach - fixed 128-pixel rows for multi-tile sprites
**Plan**: Use 128-pixel row height (enough for main 64 + bottom 64), avoiding all collisions
**Layout**:
```
Row 1 (Y=4):     [Main tiles at Y=4, bottom tiles at Y=68]
Row 2 (Y=132):   [Main tiles at Y=132, bottom tiles at Y=196]
```
No collision because each row reserves 128 vertical pixels.
**Actual result**: REGRESSION - only 3 frames load (johnny, johnny, johnny, green square)
**Analysis**: 128-pixel rows means only ~3 rows fit, and animation cycles past loaded frames
**Reverted**: Yes

### Step 9f: Limit multi-tile BMPs to 6 frames (one row)
**Plan**: If BMP has sprites > 64 tall, limit to 6 frames to leave room for bottom tiles
**Code changes**:
- Detect needsMultiTile by checking heights array
- If needsMultiTile, cap numToLoad at 6
- Load bottom tiles for all 6 frames (safe since no row 2 collision)
**Expected result**: Johnny shows feet for 6-frame animation cycle
**Actual result**: FULL REGRESSION - NO TILES AT ALL (including island!)
**Analysis**: Island uses grLoadBmpRAM (different path), so this suggests memory corruption
**Reverted**: Yes

---

## Summary of All Failures

| Trial | What was tried | Result |
|-------|----------------|--------|
| 1 | Initial multi-tile loading | BROKE ALL SPRITES |
| 6 | LoadImage timing test | Hat repeated below Johnny (wrong VRAM read) |
| 8 | Full multi-tile implementation | NO SPRITES AT ALL |
| 9a | Linked list walking only (nextTile=NULL) | SUCCESS ✓ |
| 9b | Add bottom tile loading + linking | FULL REGRESSION |
| 9c | LoadImage WITHOUT linking | FULL REGRESSION |
| 9d | rowMaxHeight tracking (no bottom tiles) | Johnny split in half |
| 9e | 128-pixel rows | Only 3 frames work, then green square |
| 9f | 6-frame limit + bottom tiles | FULL REGRESSION (even island gone!) |

## Key Observations

1. **grDrawSpriteExt linked list walking works** when nextTile is NULL (step 9a)
2. **LoadImage for bottom tiles causes full regression** even WITHOUT linking (step 9c)
3. **Changing VRAM row height causes rendering corruption** (step 9d)
4. **The regression affects even grLoadBmpRAM sprites** (island) which suggests memory/VRAM corruption
5. **Something about bottom tile LoadImage corrupts global state**

## Current Baseline State

- grDrawSpriteExt: Has linked list walking code (confirmed working with nextTile=NULL)
- grLoadBmp: NO bottom tile loading (disabled)
- Result: Johnny renders with feet cut off, island renders correctly

## Unsolved Mystery

Why does calling LoadImage for bottom tiles break EVERYTHING, including sprites loaded via completely different code paths (grLoadBmpRAM)?

Possible causes:
1. Bottom tile VRAM position overlaps with something critical (CLUT? framebuffer?)
2. safe_malloc corruption when allocating bottom tile buffer
3. srcPtr calculation for bottom tile source data is wrong, causing memory read corruption
4. DrawSync(0) interaction issue

---

### Step 9g: Frame 0 only with simpler src pointer
**Plan**: Use `src` directly (already points to row 64 after main tile copy), only for frame 0
**Code change**: `uint8 *bottomSrc = src;` instead of complex srcPtr calculation
**Expected result**: Frame 0 shows Johnny's feet
**Actual result**: PARTIAL SUCCESS - Johnny and island visible, but one frame shows hat repeated below
**Analysis**: Bottom tile IS rendering at correct position, but showing WRONG content (hat instead of feet)
**Diagnosis**: Either UV reading wrong VRAM location, or LoadImage copied wrong source rows

### Step 9h: Test reading from end of frame data
**Plan**: If BMP is bottom-up, read from last 14 rows instead of rows 64-77
**Code**: `bottomSrc = frameStart + srcRowBytes * (height - bottomH)`
**Actual result**: STILL SHOWS HAT - source data offset not the issue

### Step 9i: Test using main tile buffer for bottom tile
**Plan**: Use copyBuf (main tile data) for bottom tile LoadImage to verify VRAM/UV works
**Code**: `LoadImage(&bottomRect, (uint32*)copyBuf);` instead of bottomBuf
**Expected**: If VRAM positioning correct, should show body (same as main tile start)
**Actual result**: FULL REGRESSION - broke ALL sprite rendering including island!
**Analysis**: Something about LoadImage to Y=68 position corrupts rendering globally
**Reverted**: Yes

### Step 9j: Test with completely different VRAM X position
**Plan**: Put bottom tile at X=704, Y=4 (different texture page entirely)
**Code**: `bottomVramX = vramX + 64; setRECT(&bottomRect, bottomVramX, 4, ...)`
**Expected**: If source data is correct, should show feet at different tpage
**Actual result**: STILL SHOWS HAT - confirms source DATA is wrong, not VRAM positioning
**Key insight**: The issue is in WHAT data we're copying, not WHERE we put it

## KEY LEARNING: Source data pointer calculation is broken

After the main tile copy loop:
```c
for (uint16 y = 0; y < safeH; y++) {  // safeH = 64 iterations
    // copy row y
    src += srcRowBytes;  // advance by 32 bytes per row
}
// After loop: src = srcPtr + 64*32 = srcPtr + 2048 bytes
```

Expected: `src` points to row 64 (start of feet data at byte 2048)
Actual: Bottom tile shows HAT, which is at row 0-~10 (bytes 0-~320)

**Possible explanations:**
1. `src` variable is somehow NOT pointing to row 64 after the loop
2. The BMP data layout is different than expected (frames not contiguous?)
3. Each row has padding that we're not accounting for
4. The `src` pointer gets corrupted/reset somehow

**Next test:** Copy from explicit row 0 to confirm - if we get same result, proves `src` is at wrong location

### Step 9k: Explicit row 0 copy test
**Code**: `bottomSrc = frameStart` (row 0 = hat)
**Actual result**: Still shows hat (expected since we explicitly copied hat)

### Step 9l: Understanding texture page issue (step 9j analysis)
**Problem found**: When bottom tile was at X=704, Y=4 (different texture page):
- tpage calculated from FIRST tile: tpageX=640/64=10
- Bottom tile UV (0, 4) reads from tpage (640,0), not (704,0)
- So UV was reading from (640, 4) = main tile location, not bottom tile!
**Conclusion**: All tiles MUST be in same texture page (same X column) for shared tpage to work

### Step 9m: Back to same X column
**Code**: bottomVramX = vramX (same X), bottomVramY = vramY + 64
**Actual result**: Still hat, possibly shifted right
**Analysis**: Confirms VRAM collision theory - frame 6+ main tiles overwrite frame 0's bottom tile

## CONFIRMED: VRAM Row Collision
Frame 0 bottom tile is at Y=68, but frames 6-11 main tiles are ALSO at Y=68!
When we load frames 6+, they OVERWRITE frame 0's bottom tile data.
So when rendering frame 0's bottom tile, we read frame 6's main tile (hat) instead.

The "shifted right" observation confirms this - frame 6 is at a different X position,
so the overwritten data appears shifted when rendered.

### Step 9n: Load ONLY frame 0 to prevent collision
**Plan**: Set numToLoad = 1 to prevent any collision
**Expected**: If theory correct, frame 0 should show feet (no overwrite)
**Actual result**: SUCCESS!!! JOHNNY'S FEET ARE VISIBLE!!!

## 🎉 BREAKTHROUGH CONFIRMED 🎉

**The multi-tile rendering WORKS!** All our code is correct:
- ✅ Source data copying (rows 64-77 = feet)
- ✅ Nibble swap for PS1 format
- ✅ LoadImage to VRAM
- ✅ PS1Surface creation and linking
- ✅ grDrawSpriteExt linked list walking
- ✅ UV calculation
- ✅ Texture page sharing

**The ONLY problem was VRAM collision:**
- Frame 0 bottom tile loaded to Y=68
- Frames 6+ main tiles ALSO loaded to Y=68 (row 2)
- Later frames OVERWROTE frame 0's bottom tile
- When rendering frame 0, UV read the overwritten data (showing hat from frame 6+)

## Solution Required

Need to prevent VRAM collision. Options:
1. **Use 128-pixel rows**: Wastes VRAM, fewer frames fit
2. **Load bottom tiles to separate region**: More complex VRAM management
3. **Limit multi-tile sprites to first row only**: Only 6 frames have feet
4. **Interleave loading**: Load main+bottom for each frame before next frame

Recommended: Option 1 (128-pixel rows) or Option 4 (interleave loading)

---

## Trial 10: 128-pixel rows approach

**Date**: 2025-12-27
**Goal**: Use 128-pixel rows for multi-tile BMPs to prevent VRAM collision

### Step 10a: Detect needsMultiTile + set rowHeight=128
**Code changes**:
- Scan all BMP frame heights to detect if any > 64
- Set rowHeight = 128 for multi-tile BMPs
- Use rowHeight in row wrap code (instead of MAX_SPRITE_DIM)
- Update VRAM exhaustion check to use full sprite height

**Expected**: With 128-pixel rows, bottom tiles at Y=68 won't be overwritten by row 2 (at Y=132)
**Actual result**: FULL REGRESSION - NO SPRITES AT ALL
**Reverted**: Yes

### Step 10b: 128-pixel rows + multi-tile for frame 0 only
**Code changes**: Same as 10a but only enable multi-tile for frameIdx==0
**Expected**: At least frame 0 should show feet
**Actual result**: REGRESSION - no sprites, green box over Johnny on some frames
**Analysis**: The 128-pixel row change affects ALL frame positions, breaking UV calculations
**Reverted**: Yes

---

## Trial 11: 6-frame limit approach

### Step 11a: Limit multi-tile BMPs to 6 frames
**Code changes**:
- Detect needsMultiTile
- If needsMultiTile && numToLoad > 6, set numToLoad = 6
- Enable multi-tile code for all frames

**Rationale**: With only 6 frames in row 1, bottom tiles at Y=68 won't be overwritten
(row 2 would start at Y=68, but we never wrap to row 2)

**Expected**: 6-frame animation with feet on all frames
**Actual result**: FULL REGRESSION - NO SPRITES AT ALL (including island!)
**Analysis**: Same as Trial 9f - something about multi-tile loading for >1 frame corrupts global state
**Reverted**: Yes

---

## Trial 12: Back to 1-frame limit (current stable state)

### Step 12a: Limit multi-tile BMPs to 1 frame
**Code changes**:
- Detect needsMultiTile
- If needsMultiTile && numToLoad > 1, set numToLoad = 1
- Enable multi-tile code for all frames (only frame 0 runs)

**Expected**: Frame 0 shows feet, no animation
**Actual result**: SUCCESS - Johnny's feet visible, island renders correctly
**Analysis**: This confirms:
- Multi-tile code is correct for a single frame
- Something breaks when loading multi-tile for MORE than 1 frame
- The issue is NOT in VRAM collision (we proved that with 128-pixel rows failing too)
- The issue is in the multi-tile loading loop itself when run multiple times

---

## Current Stable State (Trial 12a)

- needsMultiTile detection: Working
- numToLoad limit for multi-tile: 1 frame only
- Multi-tile loading code: Enabled
- grDrawSpriteExt linked list walking: Working
- Result: Johnny shows feet on frame 0, no animation (stuck on frame 0)

---

## Unsolved Mystery (Updated)

Why does multi-tile loading for >1 frame break EVERYTHING?

The issue is NOT VRAM collision (we prevented that with 128-pixel rows and still got regression).

Possible causes to investigate:
1. Memory exhaustion - safe_malloc for bottomBuf × N frames
2. srcPtr calculation corrupted after first frame
3. Some global state corrupted during multi-tile loading
4. Buffer overflow in the nested copy loops
5. The `src` pointer reuse issue - after main tile copy, src points to row 64, but for frame 1+, does srcPtr get updated correctly?

**Key observation**: Even the ISLAND (loaded via grLoadBmpRAM, completely different code path) stops rendering when multi-tile fails. This suggests:
- Memory corruption affecting malloc/free
- VRAM corruption overwriting island texture area
- GPU state corruption

---

## Trial 13: 2-frame limit to isolate the issue

### Step 13a: Increase limit from 1 to 2 frames
**Code changes**:
```c
/* For multi-tile BMPs, limit to 2 frames to test if >1 works. */
if (needsMultiTile && numToLoad > 2) {
    numToLoad = 2;
}
```

**Expected**: If VRAM layout is correct, both frames should show feet
**Actual result**: PARTIAL SUCCESS - Both frames render, but frame 1's bottom tile shows frame 0's foot!

**Analysis**:
- Frame 0: Main tile (body) + Bottom tile (feet) - CORRECT
- Frame 1: Main tile (body) + Bottom tile (frame 0's feet!) - WRONG

**Key insight**: Frame 1's UV is reading from frame 0's bottom tile VRAM location, not its own.

### Step 13b: Investigate addPrim ordering

Examined `grDrawSpriteExt` primitive ordering:
```c
// Original code (WRONG ORDER):
addPrim(extOT, tpage);  // Added to OT first
addPrim(extOT, sprt);   // Added to OT second
```

**PS1 addPrim behavior**: `addPrim(ot, p)` adds primitive to HEAD of ordering table.
- Last added = first rendered
- So: sprt renders BEFORE tpage sets the texture page!

**The bug**: Each tile needs its own DR_TPAGE to set the correct texture page BEFORE the SPRT renders. But the SPRT was being added AFTER tpage, meaning it was rendered FIRST (before the texture page was set).

For frame 0: Works because tpage happens to be correct from previous state
For frame 1: SPRT renders with frame 0's tpage still active, reads wrong VRAM!

### Step 13c: Fix addPrim order
**Code change** in grDrawSpriteExt:
```c
/* Add to ordering table - sprt FIRST so tpage renders BEFORE it
 * (addPrim adds to HEAD, so last added = first rendered) */
addPrim(extOT, sprt);   // Now added first, renders SECOND
addPrim(extOT, tpage);  // Now added second, renders FIRST
```

**Expected**: DR_TPAGE sets correct texture page BEFORE SPRT renders
**Actual result**: SUCCESS!!! Both frames render with correct feet!!!

---

## 🎉🎉🎉 MAJOR MILESTONE: 2-Frame Multi-Tile Working! 🎉🎉🎉

**Date**: 2025-12-27

### The Root Cause

The PS1's `addPrim()` macro adds primitives to the **HEAD** of the ordering table, meaning:
- Last added primitive = first rendered
- Earlier added primitives render AFTER later ones

Our original code added tpage first, then sprt:
```c
addPrim(extOT, tpage);  // Added first → renders second
addPrim(extOT, sprt);   // Added second → renders first ← WRONG!
```

This caused the SPRT to render BEFORE the DR_TPAGE set the correct texture page!

### The Fix

Reverse the addPrim order:
```c
addPrim(extOT, sprt);   // Added first → renders second
addPrim(extOT, tpage);  // Added second → renders first ← NOW CORRECT!
```

Now DR_TPAGE sets the texture page, THEN SPRT renders with correct UV mapping.

### Why Frame 0 Worked Before

Frame 0 happened to work because:
1. It was often the first tile rendered
2. The GPU state/tpage from previous operations happened to be compatible
3. No other tile had changed the tpage yet

### Why Frame 1 Failed Before

Frame 1 failed because:
1. Frame 0's tpage was still active (its DR_TPAGE rendered but SPRT already rendered first)
2. Frame 1's SPRT rendered immediately (before its own DR_TPAGE)
3. Frame 1's UV coordinates read from frame 0's texture page area
4. This showed frame 0's foot instead of frame 1's foot

### Summary of the Complete Solution

1. **Multi-tile detection**: Check if any frame height > 64 pixels
2. **Frame limiting**: Limit to 2 frames (will increase as we verify stability)
3. **Bottom tile loading**: Copy rows 64+ with nibble swap, LoadImage to VRAM
4. **Linked list walking**: grDrawSpriteExt iterates all tiles with tileOffsetY
5. **CRITICAL: addPrim ordering**: Add SPRT first, then TPAGE (so tpage renders first)

### Code Locations

- Multi-tile detection: `graphics_ps1.c:561-569`
- Frame limiting: `graphics_ps1.c:571-579`
- Bottom tile loading: `graphics_ps1.c:665-720`
- addPrim fix: `graphics_ps1.c:1247-1250`

---

## Next Steps

1. ✅ ~~Verify 2-frame multi-tile works~~ DONE
2. ⬜ Increase frame limit to 6 and test
3. ⬜ Increase frame limit to full 42 frames (may need VRAM layout changes)
4. ⬜ Test with other multi-tile BMPs
5. ⬜ Verify grDrawSpriteFlip also has correct addPrim order

---

## Lessons Learned

1. **PS1 addPrim adds to HEAD**: Last added = first rendered. This is counterintuitive!
2. **DR_TPAGE must render BEFORE SPRT**: The texture page primitive sets GPU state for subsequent SPRTs
3. **Test incrementally**: Going from 1→2 frames revealed the tpage ordering bug
4. **Visual symptoms are clues**: "Frame 1 shows frame 0's foot" → reading wrong VRAM → wrong tpage active
5. **Don't assume working code is correct**: Frame 0 worked by accident, not by design

---

## Trial 14: Scaling up frame count

**Date**: 2025-12-27

### Confirmed Working: 2 frames
- 2 frames with multi-tile: WORKS - Johnny shows feet, animation plays correctly

### Test: 6 frames
**Result**: FULL REGRESSION - NO SPRITES AT ALL (including island)

**Analysis**: Something breaks between 2 and 6 frames. The issue is NOT just the addPrim ordering (that was fixed). There's another problem that manifests with more frames.

Possible causes:
1. VRAM collision returns - bottom tiles of frames 0-5 may overlap with something
2. Memory exhaustion from allocating 6 bottom tile buffers
3. Primitive buffer overflow from 12 tiles (6 main + 6 bottom)
4. srcPtr calculation corruption after multiple frames

### Test: 3 frames
**Result**: FULL REGRESSION - NO SPRITES AT ALL

**Critical Finding**: Breakpoint is exactly at frame 3 (0-indexed: frame 2)
- 2 frames: WORKS
- 3 frames: BREAKS completely
- 6 frames: BREAKS completely

**Analysis**: The third frame's loading causes the failure. This could be:
1. srcPtr calculation goes wrong after loading 2 frames + their bottom tiles
2. VRAM placement of frame 2's bottom tile overwrites critical data
3. Memory exhaustion on the third malloc for bottom tile buffer
4. Off-by-one error somewhere in the frame loop

### Next: Roll back to 2 frames and re-verify stability
