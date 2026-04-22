# PS1 Fishing 1 Water Animation — Session Worklog 2026-04-21

## Outcome so far

- Root cause of "no waves" found: **`ttmBackgroundSlot.numSprites[0] == 0`**
  after `adsInitIsland()` returns. Every downstream wave draw bails at the
  `numSprites==0` check in `grDrawSprite`.
- Root cause of the *empty slot*: `grLoadBmp(&ttmBackgroundSlot, 0,
  "BACKGRND.BMP")` inside `islandInit` cannot allocate enough contiguous heap
  for either (a) the ~93 KB `BACKGRND.PSB` stream read or (b) the ~150 KB
  raw `BACKGRND.BMP` fallback. Slot is never populated.
- The BMP/PSB files ARE on the CD. `ps1PilotActivePack.entries` is NULL on
  this path (fgpilot, pre-scene), so the "pack is authoritative, skip CD
  fallback" early-exit is NOT firing. The failure is purely heap pressure.
- The other fishing1-path BMPs (TRUNK, MRAFT) are ~10 KB and load fine;
  BACKGRND is ~10× bigger and is the only one hitting the heap ceiling at
  that moment in the scene setup.

## Evidence gathered

Ran a progressively larger on-screen probe grid (described in detail below).
Key findings verified on DuckStation:

- **Heap ceiling**: `malloc` succeeds for 4K/32K/64K/93K, fails for 150K.
- **Stream-read ceiling**: `ps1_streamRead("PSB\\BACKGRND.PSB", N)` succeeds
  for N=4K/32K, fails for N=64K/93K.
- The stream ceiling is *lower* than the raw-malloc ceiling because
  `ps1_streamReadFromCdFile` allocates **twice**: a sector-aligned staging
  buffer AND a size-exact output copy → peak ≈ 2× requested.
- `findBmpResource("BACKGRND.BMP")` returns non-NULL, has numImages>0 and
  uncompressedSize>0, but `uncompressedData` stays NULL after load (status
  code 4 = "no BMP bytes after load"), consistent with the stream/malloc
  pair failing.

## What the probe grid currently tests

A compact row of 10 colored squares at screen y=172 (green = yes/pass,
blue = no/fail):

- T27 `malloc(4 KB)` — heap baseline
- T28 `malloc(32 KB)`
- T29 `malloc(64 KB)`
- T30 `malloc(93 KB)` ← PSB file size
- T31 `malloc(150 KB)` ← raw BMP file size
- T32 `ps1_streamRead("PSB\\BACKGRND.PSB", 4 KB)` — stream baseline
- T33 `ps1_streamRead(…, 32 KB)`
- T34 `ps1_streamRead(…, 64 KB)`
- T35 `ps1_streamRead(…, 93 KB)` ← exact PSB full read
- T36 `ttmBackgroundSlot.numSprites[0] > 0` ← end-to-end success signal

If T30 green + T34/T35 blue + T36 blue: "heap OK for a 93 KB lump, but the
2×-alloc stream read can't fit". That's the fix target.

If the stream path is reduced to a 1× alloc peak (either via `memmove` or
read-direct-into-target), T34/T35 should flip green and T36 should follow.

## What's been tried that did NOT work (this session)

All three attempts below hung on the title screen — they never got to the
fishing1 scene. The fix must come later, carefully.

1. **Load BACKGRND.BMP before `grLoadScreen(OCEAN)` in `islandInit`**
   (with MRAFT shuffled to slot 1 so it wouldn't clobber slot 0). Hung on
   title. Most likely `grLoadScreen` then failed because the 93 KB PSB held
   the big heap lump.

2. **Rewrite `ps1_streamReadFromCdFile` to split head / bulk / tail sectors
   and read directly into the output buffer**. First attempt had a
   buffer-overflow bug (bulk chunk writing past `size` for non-sector-aligned
   reads); second attempt was functionally correct in paper trace but *still*
   hung on title. Probable cause: doing multiple short CdControl+CdRead
   cycles for the head/tail splice changes PS1 CD controller state enough
   that something early in boot (likely in `parseResourceFiles` or
   similar very early path) hangs.

3. **Simpler rewrite: keep the original read loop but skip the second
   `malloc` and return the sector buffer after an in-place `memmove`**.
   Also hung on title. Unclear why — memmove semantics should be identical
   to the old read-then-memcpy path.

Given attempts 2 and 3 broke the title boot but were logically equivalent
to the original for `offset==0` reads (which is what the title path uses),
something unrelated to streaming is being affected by our build changes.
Could also be an incidental BSS / code-size issue flipping something about
how the PS1 loader lays out the binary.

## Recommended next steps

- Revert `cdrom_ps1.c` to its last clean state. Keep only the probe row
  (T27-T36) + supporting accessors.
- Rebuild. Confirm title boot works and fishing1 scene reaches compose
  (even without waves, as the probe row proves we've seen before).
- Only then attempt the streaming / memory fix. Before each new attempt,
  run *just that change* and confirm boot still works — do not stack
  multiple changes into a single test.

## Fix candidates that are still on the table

1. **Free the OCEAN0X.SCR clean tiles before the BACKGRND load** — `grLoadScreen`
   keeps ~600 KB of bgTile + clean-tile memory live. If we could free those
   temporarily, the 93 KB PSB stream's 2× peak would fit.
2. **Pre-load BACKGRND.BMP earlier, *before* `grLoadScreen` runs** — but
   without breaking the OCEAN SCR load that immediately follows.
3. **Use the `ps1_streamReadFromCdFileIntoBuffered` variant** — caller-provided
   sector buffer, no malloc in the inner path. Requires plumbing through
   `grTryLoadPsb` / `grLoadBmpRAM`.
4. **Skip `grLoadScreen(OCEAN)` altogether in the fgpilot path** — compose the
   ocean base from a smaller source (or use `ISLETEMP.SCR` which already has
   the ocean baked in). Then we don't fight the OCEAN SCR for heap.
5. **Shrink one of the other persistent large allocations** — e.g. the
   title screen buffer if still held, or reduce the number of clean bgTile
   copies.

## Probe infrastructure already in place (keep)

`ads.c` exposes accessors used by the probe row:

- `adsDbgAlloc4K/32K/64K/93K/150KOk()`
- `adsDbgStream4K/32K/64K/93KOk()`
- `adsDbgBackgroundSlotSpriteCount()`

And the one-shot `adsDbgRunProbesOnce()` runs on the first compose frame
(cached thereafter), so the probes don't thrash per-frame.

## Boot-hang root cause (solved)

While isolating "HEAD + FISHING.ADS name fix" from everything else, we
proved the title-to-scene hang is caused by **integer division by zero in
`islandAnimate`** when `BACKGRND.BMP` fails to load.

`island.c`'s `islandAnimate` does:

```c
grDrawSprite(grBackgroundSfc, ttmSlot, waveX, waveY, waveSpriteNo, 0);
// ...
uint16 actualSpriteNo = waveSpriteNo % ttmSlot->numSprites[0];  // <- hang
```

Flow:

1. `fgPlayOceanRuntimeScene` calls `adsInitIsland`.
2. `islandInit` calls `grLoadBmp(&ttmBackgroundSlot, 0, "BACKGRND.BMP")`.
   Under the memory conditions present at this point in the fgpilot path,
   that load silently fails — the PSB path's 2× stream alloc (93 KB + 93 KB)
   can't find a free block, and the BMP fallback's ~150 KB alloc also fails.
   `ttmBackgroundSlot.numSprites[0]` ends up 0.
3. `islandInit` then runs the initial-wave loop:
   `for (int i=0; i < 4; i++) islandAnimate(ttmThread);`.
4. `islandAnimate`'s `grDrawSprite` call is a silent no-op (the
   `imageNo/numSprites` guard inside `grDrawSprite` returns cleanly).
5. The very next line computes
   `waveSpriteNo % ttmSlot->numSprites[0]` — an integer `%` with divisor 0
   on MIPS R3000 traps via the overflow/exception handler, which on this
   build effectively deadlocks the console (no handler progresses past it).
6. The main loop never starts, the title RAW framebuffer is never
   overwritten, so the user sees "stuck on title".

**Why it looked like a hang and not a crash:** DuckStation keeps showing
the existing framebuffer (title) if the guest CPU is stuck. From the
outside it's indistinguishable from a "600-second delay".

**Fix** (in-session): add an early return guard in `islandAnimate` before
the modulus operation:

```c
if (ttmSlot->numSprites[0] == 0)
    return;
uint16 actualSpriteNo = waveSpriteNo % ttmSlot->numSprites[0];
```

This is defensive — it also protects any other path where the wave slot
could be empty (e.g. a future scene pack change, or a CD read stall).

## Debugging lessons learned

- On PS1 / this harness, **"stuck on title" is always worth suspecting a
  guest CPU fault**, not an I/O or logic wait. Division, modulus, null
  deref, and misaligned-load faults all end the same way from a host
  observer's perspective.
- Add div-by-zero / null-slot guards around every `%` and array index
  whose divisor/index comes from `ttmSlot->numSprites[...]`. There are
  several of these in `island.c`, `graphics_ps1.c` (replay path), and
  potentially more — audit on a quiet day.
- When deciding whether a title stall is code vs. a deliberate I/O
  backoff, the fastest disambiguator is an on-screen probe row that
  updates every frame in `foregroundPilotRuntimeCompose`. If the row
  never appears, the compose loop never ran — which means either
  `foregroundPilotRuntimeStart` bailed, or the CPU faulted before we got
  there. Worth keeping a minimal probe row as standard debug
  scaffolding.
- A cheap way to confirm a CPU fault vs. a real hang in DuckStation: the
  console's own CPU-exception handler is typically quiet on PS1 titles;
  but you can open DuckStation's CPU debugger view (if available) or,
  more reliably, check whether the background audio / spinner continues
  ticking. Silent audio = CPU probably faulted.

## Resolution (landed this session)

Fishing 1 now plays through the full scene with three shoreline waves
animating in parallel. The fix is a clean three-part change:

### 1. Pre-load BACKGRND.BMP before bg-tile allocation

Added `adsPilotPreloadBackgrndBmp()` in `ads.c`, called at the very top
of `fgPlayOceanRuntimeScene` — *before* `fgInitVisiblePipeline` runs and
allocates the 614 KB of empty bg tiles. With a fresh heap at that moment
the ~93 KB PSB (which peaks at ~186 KB during ps1_streamRead's 2× alloc)
loads cleanly.

Verified with pre-load heap-tier probes: all 4 KB / 32 KB / 64 KB / 93 KB
/ 150 KB / 200 KB allocs succeed at that moment. The naive placement
(after scene setup) fails every tier above 64 KB.

### 2. Wave-tick parity with the normal ads path

Added `adsPilotTickBackgroundWaves()` (mirrors the inline tick block in
`adsPlay`'s main loop). Called once per frame from the fgpilot main
loop. With `delay=2`, `islandAnimate` advances one wave position every 2
frames and `islandRedrawWave` carries the last-drawn wave across the
`grRestoreBgFromRects`-wiped frames in between.

Also: seed the clean baseline by calling `islandAnimate` 4× inside
`adsPilotEnableWaveBackdrop` before the clean backup — same trick that
`islandInit` uses in the normal path — so the restored baseline already
has all three wave positions and they look parallel from the first
frame.

### 3. Rect-based clean backup (option B)

Added `grSaveCleanBgRects()` / `grRestoreBgFromRects()` to
`graphics_ps1.c`. Replaces the 614 KB of full-tile clean copies with a
single 596×152 rectangle at (12, 204) — the union of the foreground pack
bbox and the wave shore area. **~181 KB instead of 614 KB**. That's the
headroom that lets the BACKGRND PSB coexist with everything else.

After the fix, at compose time all six `malloc` probes (4/32/64/93/150
KB) report green — heap is comfortable.

### Div-by-zero guard

`islandAnimate` originally did `waveSpriteNo % ttmSlot->numSprites[0]`
with no guard. If `BACKGRND.BMP` ever failed to load, that's a MIPS
integer-zero-division CPU exception — and on this build the exception
handler effectively hangs. We saw that exact hang several times before
we figured out it wasn't an I/O wait. The safety-skip in
`adsPilotEnableWaveBackdrop` bypasses the seed loop when
`numSprites[0] == 0`, so a future load failure becomes "scene plays
without waves" instead of "boot hang".

## Open follow-ups for subsequent sessions

1. **Generalize the rect-based clean backup to other scenes.** Each
   scene that uses the fgpilot path needs to declare its dynamic rect(s),
   and those rects must be computed relative to `islandState.xPos/yPos`
   and `LEFT_ISLAND`. For fishing1 (ELSE branch, no
   `storyPrepareSceneBaseByAds` call → islandState at origin) absolute
   coords work; for any scene that does shift the island, the rects need
   to shift too.
2. **Remove the probe row and heap-tier diagnostic accessors** once we've
   verified the fix on multiple scenes. They're useful scaffolding but
   add noise to the binary + bg-tile dirty area.
3. **Audit other `%`-by-`numSprites` sites** (replay path,
   `grRecordReplaySprite` lookups, etc.) — any of them could hang if
   their backing slot ever turns out empty.
4. **Consider a further scale-up**: 1/64 grid of bg tiles so compositing
   + clean coverage naturally share the same fine-grain regions. The
   rect approach is a pragmatic middle step; the finer grid is the
   long-run generalization.
