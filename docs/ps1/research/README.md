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

- Heaviest scene: `MARY.ADS tag 1 = 555.3 KB`
- Budget violations: `0 / 63 scenes`
- Maximum concurrent threads: `20`

Those numbers came from:

- [scene_analysis_output.txt](/home/hunter/workspace/jc_reborn/scene_analysis_output.txt)
- [scene_analysis_output_2026-03-17.txt](/home/hunter/workspace/jc_reborn/docs/ps1/research/scene_analysis_output_2026-03-17.txt)
- [scene_analysis_output_2026-03-17.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/scene_analysis_output_2026-03-17.json)
- [scene_analyzer.c](/home/hunter/workspace/jc_reborn/scene_analyzer.c)
- [scene_pack_plan_2026-03-17.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/scene_pack_plan_2026-03-17.json)
- [scripts/plan-scene-packs.py](/home/hunter/workspace/jc_reborn/scripts/plan-scene-packs.py)
- [PACK_MANIFEST_SCHEMA.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/PACK_MANIFEST_SCHEMA.md)
- [TRANSITION_PREFETCH_SCHEMA.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/TRANSITION_PREFETCH_SCHEMA.md)
- [DIRTY_REGION_TEMPLATE_SCHEMA.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/DIRTY_REGION_TEMPLATE_SCHEMA.md)

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

### 2026-03-18 update

Phase 7 now has an offline extractor for dirty-region candidates:

- [extract-dirty-region-templates.py](/home/hunter/workspace/jc_reborn/scripts/extract-dirty-region-templates.py)
- [DIRTY_REGION_TEMPLATE_SCHEMA.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/DIRTY_REGION_TEMPLATE_SCHEMA.md)

This produces per-pack template JSON from scene-pack manifests plus extracted
`TTM` bytecode. It is intentionally offline-only for now so the runtime stays on
the last known-good rendering path while we identify candidate scene families.

An initial `BUILDING.ADS` runtime `CLEAR_SCREEN` consumer was tested and then
backed out after later black-background regressions. The useful result is still
the offline artifact and candidate selection, but runtime restore policy remains
on the last known-good path until template use is tied to validated per-scene
state instead of a fixed family-level rect.

The current scene-level pilot picked from that process is now documented in:

- [restore_candidate_report_2026-03-18.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/restore_candidate_report_2026-03-18.json)
- [restore_pilot_spec_2026-03-18.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/restore_pilot_spec_2026-03-18.json)

Right now the active narrow targets are:

- `STAND.ADS tags 1-3`, which has only one BMP, two TTM owners, and a
  `352x140` restore envelope
- `JOHNNY.ADS tag 1`, which reuses the same generated-contract path through the
  `MEANWHIL.TTM`, `SJMSSGE.TTM`, `SJWORK.TTM`, and `THEEND.TTM` cluster
- `WALKSTUF.ADS tag 2`, which exercises `MJJOG.TTM`, `MJRAFT.TTM`, and
  `WOULDBE.TTM`, including a two-region clear/save contract in `WOULDBE.TTM`

Those pilots now have a generated C-side artifact:

- [ps1_restore_pilots.h](/home/hunter/workspace/jc_reborn/ps1_restore_pilots.h)

It is emitted from [generate-restore-pilots-header.py](/home/hunter/workspace/jc_reborn/scripts/generate-restore-pilots-header.py)
using the checked-in JSON pilot specs, so the runtime can consume a small table
of validated scene contracts instead of reaching back into research JSON by
hand.

The offline spec path is now batched too:

- [build-restore-pilot-spec.py](/home/hunter/workspace/jc_reborn/scripts/build-restore-pilot-spec.py)
  can emit one spec per recommended pilot into
  [restore_pilot_specs_2026-03-19](/home/hunter/workspace/jc_reborn/docs/ps1/research/restore_pilot_specs_2026-03-19)
- [generate-restore-pilots-header.py](/home/hunter/workspace/jc_reborn/scripts/generate-restore-pilots-header.py)
  can now promote a filtered subset from that directory into a runtime header,
  so offline pre-calculation can scale faster without turning every new spec
  into a live runtime policy immediately

That batching is now expanded to the full current ADS surface too:

- [restore_candidate_report_full_2026-03-19.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/restore_candidate_report_full_2026-03-19.json)
  ranks one recommended restore candidate per ADS family
- [restore_pilot_specs_full_2026-03-19](/home/hunter/workspace/jc_reborn/docs/ps1/research/restore_pilot_specs_full_2026-03-19)
  contains the first ten-family pre-calculated spec batch:
  `STAND`, `JOHNNY`, `WALKSTUF`, `ACTIVITY`, `FISHING`, `BUILDING`,
  `VISITOR`, `MARY`, `MISCGAG`, and `SUZY`
- the header generator now tolerates incomplete per-TTM rect rows by disabling
  those hook ids instead of crashing, which means research-grade candidates can
  move through the batch pipeline before every TTM row is runtime-ready

That same pipeline now emits scene-scoped output too:

- [restore_scene_specs_full_2026-03-19](/home/hunter/workspace/jc_reborn/docs/ps1/research/restore_scene_specs_full_2026-03-19)
  contains one restore spec per ranked scene, `63` total, so offline conversion
  no longer waits on one-family-at-a-time promotion
- [restore_rollout_manifest_2026-03-19.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/restore_rollout_manifest_2026-03-19.json)
  classifies that full scene batch into `live_proven`, `offline_ready`, and
  `blocked_entry_path`
- [restore_scene_clusters_2026-03-19.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/restore_scene_clusters_2026-03-19.json)
  groups those `63` scene specs into `34` shared restore contracts, which is
  the right unit for grouped runtime promotion
- [restore_cluster_specs_2026-03-19](/home/hunter/workspace/jc_reborn/docs/ps1/research/restore_cluster_specs_2026-03-19)
  lifts those `34` contracts into reusable cluster specs, so the next runtime
  enablement step can promote a whole contract in one move

That offline pipeline has now been tightened again: the dirty-region extractor
normalizes signed TTM rectangle origins and clamps them to visible scene bounds
before unioning. That removed bogus wrapped rects like `x=65534` /
`width=65551` from the `VISITOR` family and regenerated the full scene-spec and
cluster-spec artifact set from corrected geometry.

With the full queue emitted, live promotion is now split between:

- live pilots that are actually reachable through the current harness path
- blocked pilots that remain offline-only until their story/bootstrap route is
  reproducible

The current rollout counts are `5` live-proven scenes, `56` offline-ready
scenes, and `2` scenes blocked by entry-path reproduction. The runtime header
stays scoped to the proven set for now: `STAND`, `JOHNNY`, and `WALKSTUF`.
That next grouped slice is now underway: the runtime header consumes the shared
`STAND` cluster contract for tags `1-12`, and bounded forced runs of
`STAND.ADS 4` and `STAND.ADS 12` both stayed visually good. This is the first
real contract-sized expansion beyond the original pilot tags.

The same grouped rollout now extends to `JOHNNY` too: the runtime header keeps
the original proven singleton for `JOHNNY.ADS 1`, adds the shared contract for
`JOHNNY.ADS 2-5`, and now also carries the validated `JOHNNY.ADS 6` singleton
contract. Bounded forced runs of `JOHNNY.ADS 2`, `JOHNNY.ADS 5`, and
`JOHNNY.ADS 6` all stayed visually good, so this grouped rollout now covers
the full `JOHNNY.ADS 1-6` surface.

`STAND` has also moved beyond the first cluster. In addition to the shared
`STAND.ADS 1-12` contract, the runtime header now carries the second shared
contract for `STAND.ADS 15-16`, and bounded forced runs of `STAND.ADS 15` and
`STAND.ADS 16` both stayed visually good. That makes `STAND.ADS 1-12` plus
`15-16` the current largest live grouped rollout.

`BUILDING` was tried as the next grouped target, but both `island ads` and
`story ads` entry paths still land on bootstrap/ocean states instead of a valid
composed scene. That family stays offline-only for now; the blocker is entry
reproduction, not the generated restore contract itself.

`WALKSTUF` has now been widened to cover tags `1-3` in the live generated
header. Forced runs of `WALKSTUF.ADS 1` and `WALKSTUF.ADS 3` stayed visually
good, so the scene-scoped restore contracts are viable there too, but both
routes still report active-pack fallbacks. That makes `WALKSTUF` the next
cleanup target: the restore policy is holding, while the remaining debt is in
pack completeness/runtime reads rather than scene composition.

That next slice is now in place in narrowly-scoped form: `ttm.c` has a
scene-scoped `CLEAR_SCREEN` pilot hook for the `STAND.ADS tags 1-3` pilot
cluster that only applies to `MJAMBWLK.TTM` and `MJTELE.TTM` using the
generated header rects, instead of another family-wide restore hook.

The pilot now also tracks `TTM_UNKNOWN_1` region ids in thread state and uses
the same generated contract for `SAVE_IMAGE1` on the narrow `STAND.ADS` pilot
cluster (`tags 1-3`).

The validation harness has been tightened around real PS1 boot timing too:
forced `STAND` boots now capture a short late-frame series rather than trying to
judge the route from early title-screen frames. Under that later capture window,
the `STAND` path comes up reliably and the decoder reports
`pilot_pack ... fallbacks=0`.

With that harness in place, the next Phase 7 cut is also live: on the same
`STAND.ADS tags 1-3` route, `ads.c` no longer uses replay merge, actor
recovery, or handoff carry/injection as a correctness mechanism. A fresh forced
`STAND.ADS 1` run still held visually with pack hits and zero fallbacks, so
this is the first route where the scene-scoped restore contract is beginning to
replace the older replay-resurrection model instead of merely coexisting with
it.

That same replay-policy cut now also goes through the `JOHNNY.ADS tag 1`
pilot via the shared generated pilot table. A fresh forced
`JOHNNY.ADS 1` run still held with `pilot_pack ... fallbacks=0`.

The normal boot path also flushed out a real baseline transition regression.
The first island handoff was fading to black and then collapsing instead of
completing the takeover into the next scene. The narrow fix was in
[ads.c](/home/hunter/workspace/jc_reborn/ads.c): `adsPlayWalk()` now resets
`ttmSlots[0]` after `adsStopScene(0)`, so stale `JOHNWALK` slot state no
longer leaks into the next ADS launch. Longer normal-boot capture runs now stay
alive across that first handoff instead of dropping to a permanent black frame.

One important validation detail from that same run: `story phase=4` persisting
for several captures is expected while the handoff is still inside
`adsPlayWalk()`. `storyPlay()` only advances to phase `5` after the walk path
returns, and the walk code intentionally holds an arrival pose before it exits.
So the remaining long `phase=4` window is not by itself evidence of another
stuck story-state bug.

One useful validation note from that route: the black-backed clock in the
`MEANWHIL` sequence is not a new restore regression. The original
`MEANWHIL.TTM` script explicitly issues `DRAW_RECT 0 0 640 350` after
`SET_COLORS 5 5`, then repeatedly draws the clock backing sprite. So that card
is authored scene behavior, not a pack-path failure.

The next generated pilot now exists too:

- [restore_pilot_spec_walkstuf_2026-03-18.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/restore_pilot_spec_walkstuf_2026-03-18.json)

That keeps the runtime work on the same rails: expand the generated pilot table
one scene-scoped contract at a time instead of adding another family-wide
special case.

The pilot specs now carry explicit scene resource lists too, and the PS1
runtime primes those scene-scoped resources before play through
[ps1_restore_pilots.h](/home/hunter/workspace/jc_reborn/ps1_restore_pilots.h)
and [ads.c](/home/hunter/workspace/jc_reborn/ads.c). That is the first real
offline-to-runtime link beyond dirty-rect policy: the generated spec now drives
which BMP/SCR/TTM assets get warmed for a pilot route.

One current validation note: the pack fallback telemetry is now wired to real
extracted-file reads instead of stale counters. `STAND.ADS` still validates with
`pilot_pack ... fallbacks=0`, and the `WALKSTUF.ADS 2` first-frame actor gap is
now fixed by re-enabling only one-frame missing-actor recovery on that pilot
route. The opening frame at
`ps1-test-20260319-083913.png` now shows Johnny holding the board where the
earlier `ps1-test-20260319-083316.png` run showed only the board. `WALKSTUF`
still reports one real active-pack miss (`WOULDBE.BMP` signature `17`) during
the bounded harness route, but that remaining miss is now a secondary cleanup,
not the visible scene-entry regression.

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
- machine-readable JSON for build-time tooling
- derived heuristics for scene clustering, shared resources, transition churn, and
  ADS-family prefetch candidates

Key files:

- [scene_analyzer.c](/home/hunter/workspace/jc_reborn/scene_analyzer.c)
- [scripts/analyze-scenes.sh](/home/hunter/workspace/jc_reborn/scripts/analyze-scenes.sh)
- [Makefile.analyzer](/home/hunter/workspace/jc_reborn/Makefile.analyzer)

This means the next step is not "invent analysis from scratch." The next step is
"extend the analyzer so it emits the data the build pipeline needs."

### 4. Analyzer v2 output is now machine-readable

`scene_analyzer` now supports `--json` in addition to the original text report.

Command:

```bash
./scripts/analyze-scenes.sh --json > docs/ps1/research/scene_analysis_output_2026-03-17.json
```

Current schema highlights:

- `summary`
  - global heaviest-scene and concurrency maxima
- `derived.candidate_scene_clusters`
  - current heuristic groups scenes by ADS file as a pack-compilation starting point
- `derived.shared_resources`
  - BMP/TTM inventories shared across multiple scenes
- `derived.heaviest_transition_deltas`
  - highest-churn sequential scene-to-scene deltas for pack-boundary review
- `derived.likely_prefetch_sets`
  - ADS-family union heuristic for first-pass prefetch planning
- `scenes[*]`
  - story metadata, resource bindings, thread launches, and explicit memory
    components

Important caveat:

- transition and prefetch outputs are currently heuristic rather than proven runtime
  transition graphs
- pointer-table accounting is now explicitly PS1-sized at `4` bytes per pointer,
  rather than using host `sizeof(void *)`

### 5. Pack planner consumer exists

The first build-facing consumer of the analyzer JSON is now checked in:

```bash
./scripts/plan-scene-packs.py \
  --output docs/ps1/research/scene_pack_plan_2026-03-17.json \
  --manifest-dir docs/ps1/research/scene_pack_manifests_2026-03-17
```

What it emits:

- one aggregate plan file for the pack compiler
- one manifest per ADS-family pack
- per-pack resource aggregates with global and pack-local reference counts
- transition-driven prefetch candidates, with ADS-family fallback still available

Important caveat:

- this is a planning consumer, not a runtime loader
- the pack IDs and prefetch links are heuristic, derived from the analyzer JSON
- the manifests are intentionally shaped to make the later compiler a mechanical
  step rather than a discovery step

### 6. Scene pack compiler and generic loader exist

The compiler now consumes either one manifest or the full manifest directory and
emits concrete compiled packs for every current ADS family:

```bash
./scripts/compile-scene-pack.py --all
```

Current outputs:

- [compile-scene-pack.py](/home/hunter/workspace/jc_reborn/scripts/compile-scene-pack.py)
- [PACK_PAYLOAD_LAYOUT.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/PACK_PAYLOAD_LAYOUT.md)
- [compiled pack directory](/home/hunter/workspace/jc_reborn/docs/ps1/research/compiled_packs_2026-03-17)

What it emits:

- `pack_payload.bin`
  - deterministic raw resource blob
- `pack_index.json`
  - sector-aligned offsets, sizes, checksums, and runtime envelope metadata
- `jc_resources/packs/*.PAK`
  - staged CD-visible payloads for all current ADS families, each with a compact
    binary TOC at the front of the file

Current constraints:

- resource order is fixed as `ADS -> SCR -> TTM -> BMP`
- each resource is aligned to `2048` bytes
- this is a loader target and format draft, but the runtime now consumes the
  binary TOC embedded in each pack rather than generated C tables

Runtime hook:

- ADS loads now activate a pack-first lookup path in
  [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c)
- all current `ACTIVITY/BUILDING/FISHING/JOHNNY/MARY/MISCGAG/STAND/SUZY/VISITOR/WALKSTUF`
  families now try their staged pack payload first
- once an ADS-family pack is active, `ADS/SCR/TTM/BMP` payloads are all
  pack-authoritative and no longer fall back to the extracted-file path
- bounded DuckStation validation now decodes `pilot_pack active_pack_id=7 hits=7
  fallbacks=0` on the working scene path, so the pack path is active without
  observed extracted-asset fallback in that traversal
- the generated lookup table is now shared rather than BUILDING-specific

### 7. Transition / prefetch post-processing

The analyzer JSON is now fed through a small post-processor that turns the raw
scene sequence into more actionable planning output:

- [scene-transition-prefetch-report.py](/home/hunter/workspace/jc_reborn/scripts/scene-transition-prefetch-report.py)
- [scene_transition_prefetch_report_2026-03-17.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/scene_transition_prefetch_report_2026-03-17.json)
- [scene_transition_prefetch_report_2026-03-17.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/scene_transition_prefetch_report_2026-03-17.md)
- [TRANSITION_PREFETCH_SCHEMA.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/TRANSITION_PREFETCH_SCHEMA.md)

This post-processor adds:

- pack candidates with unioned resource bytes
- adjacent transition edges with added/shared/removed byte counts
- ranked prefetch edges based on added working set

### 8. Dirty-region and restore runtime

The PS1 runtime now has its first explicit region-restore implementation instead
of relying entirely on whole-frame background restore plus replay continuity:

- [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c) now implements
  `grSaveZone()` / `grRestoreZone()` against the clean background tile copies
- the implementation tracks one active saved zone, matching the existing PC-side
  assumption for Johnny's TTMs
- `RESTORE_ZONE` can now restore a bounded rectangle from the pristine background
  tiles during TTM playback rather than acting as a no-op on PS1
- bounded DuckStation validation still boots the working scene and decodes
  `pilot_pack ... fallbacks=0` after this change

### 9. SDL-Compat Lite contract

The first written contract for the narrow runtime boundary now lives in:

- [SDL_COMPAT_LITE_SPEC.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/SDL_COMPAT_LITE_SPEC.md)

It captures:

- the minimum gameplay-facing graphics surface
- the current PC/PS1 gap matrix
- the places where PS1 still leaks replay-era implementation details into
  gameplay-visible correctness
- ranked pack-boundary candidates for disc-layout review

The caveat remains the same: these are story-order planning heuristics, not a
validated runtime transition graph.

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
- [PACK_MANIFEST_SCHEMA.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/PACK_MANIFEST_SCHEMA.md)
- [PACK_PAYLOAD_LAYOUT.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/PACK_PAYLOAD_LAYOUT.md)
- [scene_analysis_output_2026-03-17.txt](/home/hunter/workspace/jc_reborn/docs/ps1/research/scene_analysis_output_2026-03-17.txt)
- [scene_analysis_output_2026-03-17.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/scene_analysis_output_2026-03-17.json)
- [scene_pack_plan_2026-03-17.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/scene_pack_plan_2026-03-17.json)
- [scene_pack_manifests_2026-03-17/](/home/hunter/workspace/jc_reborn/docs/ps1/research/scene_pack_manifests_2026-03-17)
- [compiled_packs_2026-03-17](/home/hunter/workspace/jc_reborn/docs/ps1/research/compiled_packs_2026-03-17)
- [scene_transition_prefetch_report_2026-03-17.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/scene_transition_prefetch_report_2026-03-17.json)
- [scene_transition_prefetch_report_2026-03-17.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/scene_transition_prefetch_report_2026-03-17.md)
- [TRANSITION_PREFETCH_SCHEMA.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/TRANSITION_PREFETCH_SCHEMA.md)
