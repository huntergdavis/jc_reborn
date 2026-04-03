# PS1 Port Debugging Guide

This document captures lessons learned while debugging the PlayStation 1 port of Johnny Reborn.

## Memory Issues

### Sprite Heap Fragmentation / Missing Body Parts (FIXED)

**Symptom:** During long ADS sequences (notably fire/fish), Johnny's sprite disappears progressively (legs -> torso -> head) or eventually freezes/crashes after many scene transitions.

**Root Cause:** `grLoadBmpRAM()` was allocating and copying a new `indexedPixels` buffer for every BMP frame load. Repeated LOAD_IMAGE churn fragmented PS1 heap and increased allocation pressure, causing unstable sprite availability over time.

**Fix:** Switched to **zero-copy indexed sprite frames**:
- `PS1Surface.indexedPixels` now points directly into the owning BMP resource's `uncompressedData` frame slice.
- Removed per-frame `malloc+memcpy` for indexed sprite data.
- Added `indexedOwned` to `PS1Surface` and made `grFreeLayer()` free `indexedPixels` only when ownership is explicit.

**Why this works:** dramatically reduces heap churn and fragmentation during animation-heavy scenes, keeping sprite data stable over long runs.

**Key files:**
- `graphics_ps1.h` (`PS1Surface.indexedOwned`)
- `graphics_ps1.c` (`grLoadBmpRAM()`, `grFreeLayer()`)

### Uninitialized nextTile Pointers (FIXED)

**Symptom:** Invalid word read errors at addresses like `0x7C005418`, `0x54542E64` (ASCII-like patterns) occurring in `grReleaseBmp` or `grFreeLayer`.

**Root Cause:** `PS1Surface` structs were allocated without initializing the `nextTile` pointer to NULL. When `grFreeLayer()` traversed the linked list, it followed garbage pointers.

**Fix:** Initialize `nextTile = NULL` in ALL PS1Surface allocations:
- `grNewEmptyBackground()`
- `grLoadBmpRAM()`
- `createEmptyBgTileRAM()`
- `createBgTile()`
- `createBgTileRAMPartial()`

### Sprite Pointer Array Initialization (FIXED)

**Symptom:** Memory corruption when releasing BMPs.

**Root Cause:** `ttmInitSlot()` only set `numSprites[i] = 0` but didn't clear the actual sprite pointer arrays.

**Fix:** Zero out all sprite pointers in `ttmInitSlot()`:
```c
for (int j=0; j < MAX_SPRITES_PER_BMP; j++) {
    ttmSlot->sprites[i][j] = NULL;
}
```

## Rendering Issues

### Black Flashing / Tearing (PARTIALLY FIXED)

**Symptom:** Screen flashes black in blocks during animation.

**Root Cause:** `LoadImage` was uploading tiles to the visible framebuffer while the display was actively scanning, causing visual tearing.

**Fix:** Call `VSync(0)` BEFORE `LoadImage` operations to ensure uploads happen during vertical blank:
```c
void grUpdateDisplay(...) {
    VSync(0);  // Wait for vblank BEFORE upload
    grDrawBackground();  // LoadImage operations
    eventsWaitTick(grUpdateDelay);
}
```

### Black Bar at Bottom of Screen (FIXED)

**Symptom:** Bottom 1/4 of screen was black.

**Root Cause:** Using `adsNoIsland()` created empty black tiles. Animations that only load partial-height backgrounds (like ISLETEMP.SCR at 640x350) don't populate the bottom tiles.

**Fix:** Load a full 640x480 ocean background before animations:
```c
grLoadScreen("OCEAN00.SCR");
grSaveCleanBgTiles();
```

### Background Not Showing After adsInitIsland() (KNOWN ISSUE)

**Symptom:** After `islandInit()` draws the island/tree/clouds to bgTile buffers, they don't appear because `grRestoreBgTiles()` in the animation loop restores from "clean" copies that don't include the island.

**Required Fix:** Call `grSaveCleanBgTiles()` AFTER `adsInitIsland()` completes to save the fully-composed island scene as the new "clean" state.

### adsInitIsland() Hangs (UNRESOLVED)

**Symptom:** Game hangs at title screen when using `adsInitIsland()`.

**Possible Causes:**
- Resource loading stalling on CD read
- Infinite loop in `islandAnimate()` called 4 times in `islandInit()`
- Memory exhaustion from loading multiple BMPs (MRAFT.BMP, BACKGRND.BMP, HOLIDAY.BMP)

**To Investigate:**
- Add debug output before/after each major step in `islandInit()`
- Check if all required BMPs are on CD
- Profile memory usage during island setup

### Missing Sprites During Animation (UNRESOLVED)

**Symptom:** Johnny disappears COMPLETELY for some frames (not just outlines - entire sprite missing) while fishing pole remains visible.

**Possible Causes:**
1. **Frame index out of bounds:** Sprite indices beyond loaded frames return NULL sprites, causing entire sprite to not render.
2. **BMP frame loading failure:** Some frames may fail to load from CD, leaving NULL pointers in sprite array.
3. **Sprite slot confusion:** Animation may be drawing from wrong BMP slot that has fewer/no frames loaded.
4. **TTM opcode skipping:** Some draw commands may be skipped due to timing or conditional logic.

**To Investigate:**
- Add logging to `grDrawSprite()` to track requested vs. available sprite indices
- Log when `ttmSlot->sprites[imageNo][spriteNo]` returns NULL
- Verify BMP resource `numImages` matches animation requirements
- Check if sprite wrapping logic (`spriteNo % numSprites`) is causing issues
- Trace TTM execution to see if draw opcodes are being executed

## A/B Evidence Log

### 2026-03-04 - Track A (in-scene multi-sprite dropout)

Observed from decoded screenshots in fishing/coconut windows:
- Replay reject bars (`ads_replay_reject_slot`, `ads_replay_reject_sprite`) can stay elevated while actor visibility blinks.
- Cumulative replay-drop pressure has been high (`drop_replay_drops` saturated), indicating draw-record list pressure.

Implemented telemetry/fix round:
- Added permanent Track-A panel rows for:
  - midscene slotGen rejects
  - midscene sprite-null rejects
  - frame draw-record volume split (`imageNo==0` proxy vs non-zero auxiliary)
  - frame and scene draw-record overflow counts
- Changed replay record capture to:
  - dedupe repeated same-key draw records within a frame
  - never overwrite the final record slot on overflow (drop instead and count it)

Validation method:
- Capture visible/missing pair from the same multi-sprite scene.
- Decode with `scripts/decode-ps1-bars.py --json`.
- Compare Track-A rows first; transition bars are interpreted separately (Track B).

### Persistent Drop Overlay (NEW)

To catch intermittent missing actors (boat/mermaid/extra scene characters) in screenshots,
PS1 now draws a persistent top-left diagnostics panel whenever any drop occurs.

Bar colors:
- **Red:** ADS thread add dropped (`MAX_TTM_THREADS` overflow)
- **Yellow:** Per-frame sprite replay dropped (`MAX_DRAWN_SPRITES` overflow)
- **Magenta:** BMP frame sheet capped (`MAX_SPRITES_PER_BMP` reached)
- **Cyan:** Short BMP load (loaded fewer frames than requested)
- **Dim white top/bottom lines:** max requested BMP frame count and minimum loaded frame count seen

Current limits:
- `MAX_TTM_THREADS = 10`
- `MAX_DRAWN_SPRITES = 255`
- `MAX_SPRITES_PER_BMP = 255`

If bars never light during missing-actor moments, the issue is likely not hard cap clipping.

## Rendering Pipeline

### Correct Frame Order
1. `grRestoreBgTiles()` - Copy clean background to working tiles
2. `ttmPlay()` - Execute TTM opcodes, draw sprites via `grCompositeToBackground()`
3. `VSync(0)` - Wait for vertical blank
4. `grDrawBackground()` - Upload tiles to framebuffer via `LoadImage()`
5. `eventsWaitTick()` - Frame timing delay

### Tile Layout
```
Screen (640x480):
+----------+----------+
| bgTile0  | bgTile1  |  <- Top row (y=0-239)
| (0,0)    | (320,0)  |
+----------+----------+
| bgTile3  | bgTile4  |  <- Bottom row (y=240-479)
| (0,240)  | (320,240)|
+----------+----------+
```

Each tile is 320x240 pixels, stored in RAM as 16-bit direct color (not VRAM).

### Clean Tile Copies
- `bgTile0Clean`, `bgTile1Clean`, `bgTile3Clean`, `bgTile4Clean`
- Used to restore pristine background before each frame's sprite compositing
- Must be saved AFTER background is fully set up (including island graphics)

## CD Resources

### Required Files for Island Setup
- `OCEAN00.SCR`, `OCEAN01.SCR`, `OCEAN02.SCR` - Ocean backgrounds
- `NIGHT.SCR` - Night mode background
- `ISLETEMP.SCR` - Island template overlay
- `MRAFT.BMP` - Raft sprites
- `BACKGRND.BMP` - Clouds, island, palm tree sprites
- `HOLIDAY.BMP` - Holiday decorations

### Currently Missing
- None identified, but verify all required resources are in `cd_layout.xml`

## Build and Test

```bash
# Incremental build and test
./scripts/rebuild-and-let-run.sh noclean

# Full clean build
./scripts/rebuild-and-let-run.sh
```

Screenshots are saved to:
`~/.var/app/org.duckstation.DuckStation/config/duckstation/screenshots/`

### Overlay-Backed Screenshot Checks

For controlled PS1 bug runs, prefer overlay-backed screenshots over manual visual comparison.
The PS1 build can now draw the same machine-readable capture overlay into the live DuckStation frame
when you launch with `capture-overlay`.

Example:

```bash
./scripts/auto-test-ps1.sh 35 --overlay "story scene 17"
./scripts/capture-duckstation-scene.sh --scene "FISHING 1" --overlay
```

Then compare a captured screenshot against expected character truth:

```bash
python3 scripts/check-character-screenshot.py \
  --image ~/.var/app/org.duckstation.DuckStation/config/duckstation/screenshots/<shot>.png \
  --expected-root host-script-review \
  --scene-label "FISHING 1" \
  --out-dir /tmp/ps1-character-check
```

This reports:
- who is onscreen
- rough positions and bounding boxes
- diff vs expected truth
- an HTML report under `/tmp/ps1-character-check/character-truth-report.html`

## Next Steps

1. Debug `adsInitIsland()` hang - add logging to isolate where it stalls
2. Fix missing sprite frames - check transparency and frame indices
3. Verify all animation frames render correctly
4. Test with full `storyPlay()` for continuous scene playback
