# PS1 Fishing 1 Water Animation Handoff

Date: 2026-04-21
Repo: `/home/hunter/workspace/jc_reborn`
Branch: `ps1`

## Goal

Get `fgpilot fishing1` to:

- keep the current correct scene-relative foreground/tree occlusion behavior
- keep the current title -> scene handoff
- animate the island water/wave sprites correctly
- do it in a low-memory way that can scale to other scenes

## Known Good Baseline

The last strong water-free foreground milestone is:

- `5ea67b35` `Make Fishing 1 foreground packs scene-relative`

This fixed the tree/Johnny mismatch by making the foreground pack scene-relative.

Other later committed milestones that are relevant to the current path:

- `44cec0a6` `Restore ocean base under foreground runtime`
- `27ac05c9` `Keep title visible during staged island base loads`
- `373c38ad` `Remove title-screen CD settle delay`

User-validated integrated scene state before wave work:

- title screen -> scene handoff works
- ocean + static island base appear
- Johnny foreground pack works
- scene-relative tree occlusion works
- waves do **not** animate

## What Has Been Proven

### 1. The wave primitive itself works on PS1

A sandbox worktree was created:

- `/home/hunter/workspace/jc_reborn_wave_sandbox`
- branch `ps1-wave-sandbox`

A minimal `fgpilot wavetest` was added there:

- black background only
- no island
- no Johnny
- no FG pack
- just 4 `BACKGRND.BMP` wave sprites drawn each frame

User validation:

- black-screen `wavetest` animated correctly
- after one bad island-pop regression, the corrected version was "super legit working"

So:

- PS1 can animate the wave sprites
- `BACKGRND.BMP` wave frames are valid
- the basic draw path works in isolation

### 2. The Fishing 1 integrated failure is not a host-data absence issue

Earlier investigations established:

- host source contains moving water pixels / wave draw state
- earlier `BG1` / backdrop pack experiments did generate changing data
- wave data was not simply "missing"

### 3. Water failure is integration/runtime-side

Because black-screen `wavetest` works but integrated `fgpilot fishing1` does not, the bug is in the integrated scene path:

- clean base construction / restore semantics
- when dynamic water is drawn relative to that base
- or how integrated runtime composition/upload suppresses or overwrites those changes

## Current Technical Findings

### `grLoadBmp()` is RAM-path only on PS1

In `graphics_ps1.c`:

- `grLoadBmp()` immediately calls `grLoadBmpRAM()`
- the old VRAM/OT path is disabled

That means `grDrawSprite()` for `BACKGRND.BMP` in PS1 normally goes through:

- `grCompositeToBackground()`
- dirty-rect marking
- `grDrawBackground()` upload

So this is **not** an OT-vs-RAM issue anymore.

### Integrated main loop shape

`fgPlayOceanRuntimeScene()` currently does:

1. build static scene base
2. `grSaveCleanBgTiles()`
3. start foreground runtime
4. frame loop:
   - `grBeginFrame()`
   - `grRestoreBgTiles()`
   - `grUpdateDisplay(NULL, NULL, NULL)`
   - `foregroundPilotRuntimeAdvance()`

Inside `grUpdateDisplay()`:

1. `VSync(0)`
2. if `foregroundPilotRuntimeActive()` -> `foregroundPilotRuntimeCompose()`
3. `grDrawBackground()`
4. `eventsWaitTick(grUpdateDelay)`

So integrated dynamic wave drawing must survive:

- `grRestoreBgTiles()`
- any draws inside `foregroundPilotRuntimeCompose()`
- `grDrawBackground()` upload

### Foreground pack path uses direct compositing

Foreground pack frames go through:

- `fgBlit16ToBackgroundRect()`
- `grCompositeDirect16ToBackground()`

That path marks dirty rows directly and does **not** use sprite OT primitives.

### Some direct helper paths clear `currDirty*`

In `graphics_ps1.c`, these functions explicitly zero `currDirtyMinY/MaxY`:

- `grRestoreBackgroundRectForFrame()`
- `grRestoreAndCompositeDirect16BackgroundRectForFrame()`

Those are dangerous in mixed-draw scenarios because they can wipe previously dirtied rows in the same frame.

The main integrated scene-pack path is currently using `grCompositeDirect16ToBackground()`, which does not clear `currDirty*`, but this area remains suspect and should be watched carefully.

## What Has Been Tried And Failed

### High-memory backdrop frame streams

Tried:

- `BG1` 16-bit backdrop frame packs
- dense backdrop frame playback
- catch-up logic on backdrop frame playback

Outcome:

- waves still did not visibly animate
- memory usage ballooned
- one prelude-baseline attempt blew `FISHING1.BG1` up from about `6.6 MB` to about `75 MB`
- this path also increased crash / blank-screen risk

Conclusion:

- full-frame backdrop streams are the wrong memory model for PS1 here

### Draw-pack replay variants

Tried:

- `FOC`/draw-pack backdrop replay
- host visible-draw replay
- `BACKGRND.BMP` draw-list replay
- sparse and dense timing variations

Outcome:

- user repeatedly observed absolutely no water motion

Conclusion:

- asset-side variations were not fixing the actual runtime integration bug

### Native backdrop thread reuse

Tried:

- `adsPilotComposeIslandBackdrop()`
- `adsPilotAdvanceIslandBackdrop()`
- `islandComposePilotWaves()`
- related backdrop-thread timing/catch-up variants

Outcome:

- user still saw no water animation in `fishing1`

### Wrong-path proof overlays

Several "proof" attempts were invalid or misleading:

1. Fresh per-runtime `BACKGRND.BMP` proof slot
   - bad because `grLoadBmpRAM()` can silently short-load or fail under memory pressure
   - also made the build extremely slow

2. Scene-state / tide / island-position guesses
   - user correctly pushed back on these as not explaining total absence of motion

3. Repainting waves over sprites with a new slot
   - also invalid under memory pressure if the slot never loaded

## Most Important Validated Sandbox Mechanism

The black-screen sandbox that worked used a very simple loop:

1. `fgInitVisiblePipeline()`
2. `fgInitBlackBackground()`
3. preload `BACKGRND.BMP` once
4. each frame:
   - `grBeginFrame()`
   - `grRestoreBgTiles()`
   - `grDrawSprite(grBackgroundSfc, &slot, x, y, spriteNo, 0)` for 4 waves
   - `grUpdateDisplay(NULL, NULL, NULL)`

That worked.

So the exact delta between:

- that sandbox loop
- and integrated `fgpilot fishing1`

is where the bug lives.

## Current Dirty Worktree State

At the time of writing, the worktree is dirty with ongoing wave-debugging changes, including:

- `ads.c`
- `ads.h`
- `foreground_pilot.c`
- `island.c`
- `island.h`
- `config/ps1/cd_layout.xml`
- `scripts/export-fishing1-foreground-pilot.sh`
- `ttm.c`
- generated files like `generated/ps1/foreground/FISHING1.FOC`

The latest attempted diagnostic change was:

- remove the fresh proof-slot allocation entirely
- reuse the already-loaded island background slot via:
  - `adsPilotComposeWaveProofOverlay(uint32 vblankCount)`
  - `islandComposePilotProofWaves(struct TTtmThread*, uint32)`
- draw those proof waves before `grUpdateDisplay()` in `fgPlayOceanRuntimeScene()`

User result on that family of proof builds:

- still no visible wave overlay
- speed badly degraded in some builds

So the integrated path is still not surfacing even deliberately wrong wave draws.

## Best Current Hypothesis

There is a runtime integration bug in the visible frame pipeline, likely one of:

1. dynamic wave draws are being overwritten later in the same frame
2. dirty-row bookkeeping for integrated scene frames is wiping or skipping the wave rows
3. clean-base restore + foreground compose ordering is neutralizing the dynamic wave changes
4. integrated scene base construction leaves the wrong clean baseline, so live water changes never survive to upload

What is **not** currently a strong hypothesis:

- missing wave assets
- host export not containing wave data
- PS1 not being able to animate the wave sprites

Those have effectively been disproved.

## Recommended Next Debugging Boundary

Stop trying new export formats until the integrated runtime path is pinned down.

The right next comparison is:

### Sandbox working path

- black base
- draw waves
- upload

### Integrated failing path

- restore clean scene base
- draw dynamic waves
- draw foreground pack
- upload

The next agent should prove exactly where the dynamic wave draw disappears:

1. Does the integrated loop actually call the wave draw every frame?
2. After the wave draw, are the expected dirty rows still marked?
3. After foreground pack compose, are those dirty rows still marked?
4. Does `grDrawBackground()` upload those rows?
5. Is the clean base itself already containing a static wave frame that visually masks the motion?

## Practical Constraint

Keep the solution low-memory.

Do **not** go back to full decoded backdrop frame buffers as the primary solution.

The likely scalable solution is still:

- static scene base recipe
- tiny dynamic base-layer draw replay
- visible-pixels-only scene-relative foreground pack

But the integrated runtime bug must be fixed first.

## Short Version

- Tree/Johnny foreground is in good shape.
- Water animation works in a black-screen sandbox.
- Water animation does **not** appear in integrated `fishing1`.
- This is now clearly an integrated runtime composition/upload bug, not an asset-generation bug.
- The next agent should debug the runtime draw/dirty/upload boundary, not invent more export variations.
