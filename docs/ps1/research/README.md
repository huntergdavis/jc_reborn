# PS1 Rendering Simplification Research

Date: 2026-03-17

## Goal

Replace the current PS1-specific replay/compositing workaround model with a simpler,
more deterministic rendering architecture that uses offline preprocessing and ISO
space to recover SDL-like behavior within PS1 RAM limits.

This research package is intentionally biased toward simplification. It does not
argue that the current runtime-heavy approach is the right long-term model.

## Executive summary

The analyzer data and current code suggest that the core problem is not that the
content fundamentally does not fit in RAM. The current static estimator says:

- Heaviest scene: `MARY.ADS tag 1 = 556.1 KB`
- Budget violations: `0 / 63 scenes`
- Maximum concurrent threads: `20`

Those numbers came from:

- [scene_analysis_output.txt](/home/hunter/workspace/jc_reborn/scene_analysis_output.txt)
- [scene_analysis_output_2026-03-17.txt](/home/hunter/workspace/jc_reborn/docs/ps1/research/scene_analysis_output_2026-03-17.txt)
- [scene_analyzer.c](/home/hunter/workspace/jc_reborn/scene_analyzer.c)

That does not mean runtime RAM pressure is solved. It means the current bug surface
is more about semantic mismatch than raw impossibility:

- PC assumes persistent SDL layers with straightforward blit semantics.
- PS1 currently emulates that with background-tile restore, sprite replay records,
  actor recovery, and scene handoff heuristics.
- Those heuristics are exactly where disappearing, ghosting, and double-painting
  regressions keep appearing.

The most promising direction is:

1. Define a strict `SDL-Compat Lite` runtime contract for PS1.
2. Move complexity into offline asset compilation and static scheduling.
3. Spend ISO space on pretranscoded, prevalidated, scene-oriented data.

## Current facts from the repo

### 1. The PC and PS1 rendering models do not match

PC path:

- Composes `background + savedZones + thread layers + holiday layer`.
- Uses persistent per-thread `ttmLayer` surfaces.
- Relies on direct sprite blits plus save/restore zone behavior.

Key files:

- [graphics.c](/home/hunter/workspace/jc_reborn/graphics.c)
- [graphics.h](/home/hunter/workspace/jc_reborn/graphics.h)

PS1 path:

- Restores clean background tiles, composites indexed sprites into RAM tiles, then
  uploads those tiles every frame.
- Keeps replay records in thread state and rebuilds visual continuity from those
  records.
- Has partial or empty implementations for several SDL-like primitives and
  save/restore calls.

Key files:

- [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c)
- [graphics_ps1.h](/home/hunter/workspace/jc_reborn/graphics_ps1.h)
- [ads.c](/home/hunter/workspace/jc_reborn/ads.c)

### 2. The project already uses ISO space to trade for RAM, but inconsistently

The PS1 port already bypasses resource-file decompression for hot assets and loads
pre-extracted files from disc:

- `BMP/`
- `SCR/`
- `TTM/`
- `ADS/`

Key file:

- [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c)

That is already the right general instinct. The issue is that it stops at
"pre-extracted original assets" rather than going all the way to "PS1-runtime-native
compiled assets."

### 3. Static analysis is already strong enough to drive compilation decisions

The scene analyzer already computes:

- per-scene BMP/SCR/TTM/ADS usage
- estimated peak memory
- sprite frame counts
- concurrent thread counts
- global heavy-scene rankings

Key files:

- [scene_analyzer.c](/home/hunter/workspace/jc_reborn/scene_analyzer.c)
- [scripts/analyze-scenes.sh](/home/hunter/workspace/jc_reborn/scripts/analyze-scenes.sh)
- [Makefile.analyzer](/home/hunter/workspace/jc_reborn/Makefile.analyzer)

This means the next step is not "invent analysis from scratch." The next step is
"extend the analyzer so it emits the data the build pipeline needs."

## Recommended architecture

### Recommendation A: Compiled scene packs

This is the highest-leverage option.

Instead of loading generic BMP/SCR/TTM/ADS assets at runtime and then dynamically
figuring out what must stay live, build one compiled pack per ADS tag or tag-cluster.

Each pack should contain:

- resolved TTM set
- scene-local sprite banks
- pretranscoded sprite frame data
- precomputed metadata for thread maxima
- precomputed memory envelope for enter / steady / exit phases
- optional prefetch links to likely next packs

Runtime effect:

- less generic resource management
- fewer live asset formats
- deterministic memory behavior
- less reason for replay heuristics

Tradeoff:

- more ISO use
- more offline tooling
- need to choose pack granularity carefully

### Recommendation B: Offline sprite transcoding to PS1-native blit format

Convert every sprite frame offline into a format optimized for the actual PS1 draw
path, not for the original file format.

Strong candidates:

- opaque span tables per scanline
- simple RLE by span
- pre-flipped spans for horizontal mirroring
- nibble order fixed ahead of time
- tile split decided offline rather than at load time

Why this matters:

- runtime sprite draw becomes a deterministic span blitter
- fewer per-pixel branches
- fewer chances to disagree with PC semantics
- removes load-time format quirks from gameplay execution

This is a better use of ISO space than repeatedly paying for runtime interpretation.

### Recommendation C: SDL-Compat Lite runtime contract

Define a narrow runtime API that matches what the engine actually needs.

Minimum contract:

- acquire/release logical layer
- begin frame / present frame
- draw sprite / draw flipped sprite
- draw rect / line / pixel
- set clip zone
- copy/save/restore zone

Behavioral guarantees:

- per-thread layer persistence
- transparent blit semantics
- deterministic present order
- working save/restore for at least the current script patterns

The main design rule is:

Do not let ADS/TTM gameplay logic know about replay records, actor recovery, or
background-tile juggling.

### Recommendation D: Scene-local frame cache with static prefetch

If full compiled scene packs are too large as a first step, the fallback direction is
a bounded frame cache driven by static scene knowledge.

That means:

- keep only scene-local active frames in RAM
- prefetch likely next frames or next scene pack
- evict by deterministic schedule, not general LRU guesswork

This is weaker than compiled packs, but still much better than "runtime discovers
everything dynamically."

## What should move offline

These are the best candidates for static or build-time computation:

1. Sprite frame transcoding
- Convert BMP sprite sheets into PS1-native draw records.

2. Flip variants
- Precompute flipped versions when that reduces runtime branches and edge cases.

3. Tile splitting
- Decide how frames are split across 64px texture constraints offline.

4. Dirty-region templates
- Precompute common dirty rectangles for stable sequences like walking, fire, fish,
  coconut, and note scenes.

5. Scene memory envelopes
- Emit per-scene "must reside" and "can stream" sets.

6. Transition handoff data
- Emit exact persistence requirements at walk-to-scene and scene-to-scene boundaries.

7. CD layout hints
- Group pack files physically to reduce seek penalties.

## What should stay runtime

Runtime should be limited to:

- running ADS and TTM logic
- opening the correct compiled pack
- drawing from a deterministic surface API
- small bounded frame caching if needed
- presenting layers in a fixed order

Runtime should not keep growing new correctness heuristics for:

- actor continuity
- replay recovery
- draw-record resurrection
- dynamic sprite identity matching

Those are symptoms of the wrong boundary.

## Research conclusions

### Best target state

The strongest long-term target is:

`Compiled scene packs + offline-transcoded sprites + SDL-Compat Lite runtime`

That combination gives the biggest simplification win and aligns with the fact that
Johnny Castaway content is fixed, finite, and highly analyzable.

### Best incremental path

If the team wants lower risk, use this order:

1. Extend analyzer to emit machine-readable scene data.
2. Build an offline sprite transcoder for one problematic content family.
3. Implement SDL-Compat Lite boundary.
4. Route one scene family through compiled packs.
5. Expand scene-pack coverage once correctness is proven.

### What not to do

Avoid investing heavily in deeper versions of the current replay-based model.

It may still be useful for short-term bug fixing, but it is not a good final
architecture. It leaks PS1-specific failure modes into gameplay behavior.

## External references

These are useful for the build/runtime tradeoff discussion:

- PSX-SPX CD-ROM drive notes:
  https://psx-spx.consoledev.net/cdromdrive/
- PSX-SPX MDEC reference:
  https://psx-spx.consoledev.net/macroblockdecodermdec/
- PSn00bSDK texture / CLUT tutorial mirror:
  https://www.breck-mckye.com/psnoobsdk-docs/chapter1/3-textures.html

Why they matter:

- CD reads are fast enough to support deliberate streaming, but need disciplined
  buffering and sector handling.
- 4-bit indexed textures and CLUT constraints are native strengths of the PS1.
- MDEC exists, but it is a specialized JPEG-style path and is not automatically the
  right fit for general sprite animation assets.

## Related files in this research package

- [BACKLOG.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/BACKLOG.md)
- [IMPLEMENTATION_PLAN.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/IMPLEMENTATION_PLAN.md)
- [scene_analysis_output_2026-03-17.txt](/home/hunter/workspace/jc_reborn/docs/ps1/research/scene_analysis_output_2026-03-17.txt)
