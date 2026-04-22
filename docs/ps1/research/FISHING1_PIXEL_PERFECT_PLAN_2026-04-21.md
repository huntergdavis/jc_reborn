# Fishing 1 Pixel-Perfect Plan — 2026-04-21

## Goal

Get the fishing1 fgpilot scene visually identical to the original PC
version of Johnny Castaway, covering every `islandState`-driven variation
the scene supports (tide, raft stage, holiday, night, island position,
etc.). This is Stage 1 of a larger effort to pixel-perfect every scene.

## Working mode

One step at a time. For every step:

1. I build exactly one targeted change.
2. The build + DuckStation launch runs.
3. You look at the result and say whether it's right, and if not, what's
   off.
4. We iterate on that step, or move to the next one.

Verification is a visual check by eye, comparing against your memory of
the PC version — no screenshot diffing, no automated perceptual
compare, no reference image. If your eye says it's right, it's right.

## Approach: Path A (bespoke, stay on ELSE branch)

We tried switching to the IF branch (full `adsInitIsland`) first — it
produced multiple regressions (tree missing, Johnny pack mis-aligned,
BACKGRND failing to re-load after `ttmInitSlot` zeroed our preload).
Reverted.

Instead we stay on the current **ELSE branch** (known-good working state
from commit `74bb0c24`: waves animate, scene plays through, no
ghosting) and **add `islandState`-driven variations on top of it
manually**. We keep the memory layout we already tuned (preloaded
BACKGRND in `ttmBackgroundSlot`, rect-based clean backup).

Important note: **fgpilot mode does not run the original `FISHING.ADS`
script or its TTMs.** Johnny's actions come from the pre-captured
foreground pack (`FISHING1.FG1`) — pixel snapshots from the host build.
Scene base drawing (island, trunk, raft, clouds, waves, holiday
overlay) comes from `island.c` functions (`islandInit`,
`islandAnimate`, `islandInitHoliday`). We call these directly with a
controlled `islandState` in our ELSE-branch glue. Result is pixel-
identical to calling `adsInitIsland` — same drawing code, just
different glue.

## Step list

### Step 1 — Boot-override tokens for `islandState`

Without this we can't reliably test each variation. The `BOOTMODE.TXT`
parser already handles `fgpilot <scene>` — extend it to accept
key=value tokens after the scene name:

    fgpilot fishing1 day=N tide=low|high raft=N holiday=N night=0|1 \
                     islandx=N islandy=N

Most of these map to existing forced-variables (`storyForcedCurrentDay`,
`hostForcedLowTide`, `hostForcedRaftStage`, `hostForcedIslandX/Y`). Some
need new hooks (`holiday`, `night`). Parse in `jc_reborn.c`'s boot-
override handling and apply to `islandState` at the top of
`fgPlayOceanRuntimeScene` (after the current preload, before the ELSE
branch draws).

**You verify**: setting `fgpilot fishing1 tide=low` visibly flips wave
positions to the low-tide set; setting `night=1` flips to `NIGHT.SCR`;
etc. (If the ELSE branch doesn't yet respond to a token, we just see no
change — that's fine, the step is "parser wired, overrides present,
ready to drive subsequent steps".)

### Step 2 — Boot-override for `islandState` via `BOOTMODE.TXT`

Add parse tokens so we can pin variations for testing:

    fgpilot fishing1 day=N tide=low|high raft=N holiday=N night=0|1 \
                     islandx=N islandy=N

Most of these already correspond to existing `storyForced*` /
`hostForced*` variables — we just need the `BOOTMODE` parser to recognize
the `key=value` tokens and feed them in. Without this we can't reliably
exercise variations one at a time.

**You verify**: each token actually changes the rendered scene as
expected.

### Step 3 — High-tide baseline

    fgpilot fishing1 day=1 tide=high raft=1 holiday=0 night=0 islandx=0 islandy=0

**You verify**: looks like high-tide fishing1 on the PC.

### Step 4 — Low-tide

    fgpilot fishing1 day=1 tide=low raft=1 holiday=0 night=0 islandx=0 islandy=0

**You verify**.

### Steps 5–9 — Raft stages

`raft=1`, `raft=2`, `raft=3`, `raft=4`, `raft=5`. One step per stage.
**You verify each**.

### Steps 10–13 — Holidays

- 10: `holiday=1` — Halloween
- 11: `holiday=2` — St. Patrick's
- 12: `holiday=3` — Christmas
- 13: `holiday=4` — New Year

**You verify each**.

### Step 14 — Night mode

    fgpilot fishing1 day=1 tide=high raft=1 holiday=0 night=1 islandx=0 islandy=0

**You verify**.

### Step 15 — Island position variations

A couple of sample `(islandx, islandy)` pairs from the `VARPOS_OK` ranges
the original randomizer uses:

- `islandx=-200 islandy=-20`
- `islandx=-60 islandy=-60`
- `islandx=-114 islandy=14`

**You verify** each looks sane and matches how the PC places the island.

### Step 16 — Anything else you spot

Whatever fishing1 variation or detail you notice is off that isn't one of
the above — coconuts state, seagulls, boat, banners, rain/weather,
sparkles, special FX, etc. Handled one at a time.

### Step 17 — Cleanup / handoff

Decide: keep the `BOOTMODE` override tokens as a permanent dev
affordance (documented), or gate them behind a debug flag. Either way,
commit a final "pixel-perfect fishing1" marker so we can return to it as
the known-good baseline when we generalize to other scenes.

## After this document is done

Stage 2 (future): apply the same process to fishing2 and onward; build
the scene-specific dirty-rect table needed for the rect-based clean
backup; generalize island-relative offset math so the rects move with
`islandState.xPos` / `LEFT_ISLAND`.

Stage 3 (future): Johnny walk transitions between scenes, holiday
toggles at the start-screen level, direct joystick control mode — per
the larger master plan.
