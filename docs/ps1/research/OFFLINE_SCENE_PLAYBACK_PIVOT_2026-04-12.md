# Offline Scene Playback Pivot

Date: 2026-04-12
Status: Proposed pivot
Owner: PS1 runtime / content pipeline

## Why This Exists

The current PS1 rendering effort has spent too much time on runtime
composition correctness:

- sprite lifetime
- replay continuity
- restore/upload correctness
- scene handoff state carry
- route-specific startup behavior

That work has produced useful tooling and narrowed several boundaries, but it
has not produced proportional product progress. The project goal is not "prove
that a dynamic PS1 compositor can exactly reproduce the PC scene graph at
runtime." The real goal is to ship a stable PS1 version that:

- displays story scenes correctly
- keeps Johnny pixel-perfect
- supports sound, pause, controller skip, and later gameplay work

This document evaluates a strategic pivot:

- stop treating story scenes as fully dynamic runtime composition problems
- move them to offline-authored playback content
- keep the PS1 runtime focused on simple display/compositing responsibilities

## Executive Summary

The naive version of the idea is not viable:

- full-frame `640x480` prerendered playback for all `63` scenes does not fit in
  the available CD budget

But the underlying direction is viable if narrowed:

- use offline-authored scene playback for story scenes
- do not store full-screen raw frames
- store only the animated foreground/actor plates and keep background/ocean as
  runtime layers or low-cardinality state sets
- use indexed palettes, rectangle crops, frame deltas, and scene-local packet
  streams

Recommended pivot:

1. Keep island/gameplay/menu/controller as live runtime systems.
2. Convert story scenes to offline playback contracts.
3. Start with a hybrid format:
   - static background base
   - optional ocean/island runtime layer
   - prerendered actor/foreground plates per frame
   - per-frame metadata for input windows, pause, skip, and audio sync
4. Pilot the system on:
   - `FISHING 1`
   - one `BUILDING` scene
   - one simpler validated scene

This is the most credible path to pixel-perfect Johnny without continuing the
current runtime bug treadmill.

## Hard Storage Reality

### Full-screen raw storage does not fit

At `640x480`:

- 16-bit frame: about `614,400` bytes (`600 KB`)
- 8-bit indexed frame: about `307,200` bytes (`300 KB`)

For `63` scenes:

- `5 fps` for `30 sec` each:
  - `9,450` frames
  - about `5.5 GB` at 16-bit
  - about `2.8 GB` at 8-bit
- `10 fps` for `30 sec` each:
  - about `11.1 GB` at 16-bit
  - about `5.5 GB` at 8-bit

That is before:

- holiday variants
- raft/no-raft variants
- alternate island placement/state variants
- duplicate prerender storage for boot/title lead-ins or timing slack

Conclusion:

- full-screen prerendered `640x480` story playback is not CD-feasible

### Current disc usage suggests room for a smarter offline format

Current checked-in disc image:

- `[jcreborn.bin](/home/hunter/workspace/jc_reborn/jcreborn.bin)` is about `38 MB`
- `[jc_resources/extracted](/home/hunter/workspace/jc_reborn/jc_resources/extracted)` is about `5.5 MB`

So there is substantial unused disc budget relative to the current project
state. The pivot is only viable if that budget is spent on compressed,
structured playback data, not naive full-screen frames.

## Recommended Architecture

## Core Decision

Story scenes become offline-authored playback units.

The PS1 runtime no longer tries to reconstruct those scenes from:

- live TTM script execution
- live sprite ordering
- replay/recovery continuity
- dynamic restore heuristics

Instead, a story scene becomes:

- a deterministic playback stream
- with explicit frame timing
- explicit plate placement
- explicit audio sync
- explicit pause/skip/transition metadata

## Display Model

Use a hybrid compositor:

1. Background layer
   - static or low-cardinality background state
   - ocean/island can remain runtime if that is cheaper than storing it
2. Foreground playback layer
   - prerendered actor/foreground plate frames
   - cropped to the smallest stable region that contains Johnny and other
     transient scene elements
3. UI/pause overlay layer
   - live runtime, unaffected by cutscene rendering internals

This is the key trade:

- spend offline preprocessing effort
- to eliminate runtime correctness ambiguity

## Pixel-Perfect Johnny Requirement

This pivot only makes sense if Johnny remains exact.

That requires:

- PC-side prerender generation from the real host renderer
- no hand-rebuilt sprite composition on PS1
- no runtime re-simulation of sprite layering
- scene-local palette/plate generation that preserves the exact Johnny pixels
  from the PC source frames

Johnny is then "pixel-perfect by construction" because PS1 shows the exact
offline-authored plate, not a reconstructed approximation.

## Proposed Playback Asset Format

Use a new scene playback family, conceptually separate from the current pack
system.

Suggested on-disc structure:

- `SCN/<scene_id>.SPK`
  scene playback pack
- `SCN/<scene_id>.IDX`
  frame index / timing / metadata

Per scene payload:

- one background descriptor
  - static base image or background-state table
- one optional ocean/island descriptor
  - if runtime-generated, store only parameters
  - if prerendered, store small state atlas, not every full frame
- one foreground plate stream
  - rectangle bounds per frame
  - indexed pixel payload
  - optional delta-from-previous payload
- one timing table
  - display duration
  - frame-to-audio timing
  - skip allowed / pause allowed markers
- one interaction metadata table
  - future controller skip windows
  - subtitle/caption sync hooks

### Plate encoding

Per frame:

- `x, y, width, height`
- palette id
- payload mode
  - full indexed blit
  - XOR/delta patch
  - RLE patch
  - repeat previous

Strong default:

- 8-bit indexed plates
- scene-local palette
- row-oriented RLE or LZ-style compression
- delta patching only if it materially wins in the pilot

Do not start with a fancy codec. Start with the simplest format that yields a
real disc win.

## Why Hybrid Plates Are Better Than Full-FMV

A full video path would also solve correctness, but it has worse tradeoffs:

- more aggressive downscale/compression pressure
- less reuse of existing background behavior
- less flexibility for mixed runtime/interactive content
- more likely to look soft or artifacted

Hybrid plates keep the expensive visual correctness where it matters:

- Johnny
- foreground actors
- transient props

while leaving the broad background/ocean space cheap.

## Runtime Responsibilities

The PS1 scene playback runtime should only do:

1. Load scene playback metadata
2. Prepare background/ocean layer
3. For each playback tick:
   - decode plate payload
   - blit plate into framebuffer/working surface
   - present
4. Obey pause / skip / exit / controller actions
5. Keep audio in sync

It should not:

- discover scene resources dynamically
- run TTM/ADS logic for visual correctness
- depend on replay continuity
- depend on restore-pilot heuristics

## Variant Handling

The user concern is valid:

- holidays
- raft/no-raft
- island state changes

These should not explode storage if handled correctly.

Recommended rule:

- only offline-author the variants that actually produce different visual
  outputs in story playback

Most scenes should not multiply across every theoretical world state. Instead:

- scene compiler determines which background plate family is actually used for
  that route
- per-scene manifest records the exact chosen variant

Likely storage structure:

- common foreground stream reused across variants when possible
- different background descriptors only when truly needed

## Feasibility Gate

This pivot should be accepted only if the pilot demonstrates all three:

1. Johnny is exact
2. runtime implementation is much simpler than the current compositor path
3. disc usage scales acceptably

## Pilot Plan

Pilot scenes:

1. `FISHING 1`
2. one `BUILDING` scene
3. one already-healthy scene for baseline comparison

### Pilot deliverables

#### Stage 1: Offline analysis

For each pilot scene, export from host:

- full frame sequence
- per-frame difference against previous frame
- minimal bounding rectangle of changed actor/foreground region
- palette statistics
- background stability regions

Questions to answer:

- how large is the useful foreground plate rectangle really
- how often can frames be represented as deltas
- how many distinct palettes are needed
- whether ocean/island should remain runtime or be part of the plate stream

#### Stage 2: Storage simulation

For each pilot, estimate disc size under:

1. full-screen 8-bit
2. cropped 8-bit full-frame plates
3. cropped 8-bit delta plates
4. cropped 4-bit where acceptable

Acceptance target:

- hybrid cropped format must be dramatically smaller than full-screen storage
- projected full `63` scene rollout must fit well under the practical disc
  budget with headroom for sound and future assets

#### Stage 3: PS1 playback prototype

Implement a small runtime path that:

- loads one pilot `.SPK`
- shows background
- plays foreground plates
- supports pause and scene skip

No attempt at full generic cutscene engine yet.

#### Stage 4: Visual validation

Compare PS1 playback against host source frames for the pilot:

- Johnny exactness
- timing drift
- visible compression artifacts
- frame-edge artifacts around the plate region

## Cost/Benefit Compared To Current Path

## Benefits

- eliminates large classes of sprite correctness bugs
- moves complexity to PC preprocessing, where iteration is cheaper
- makes story playback deterministic
- gives a stable base for:
  - sound
  - pause
  - skip
  - controller input
  - later gameplay work

Most importantly:

- it changes the problem from "simulate PC layering on PS1" to "display the
  already-correct scene on PS1"

## Costs

- requires a new content build pipeline
- requires scene capture/export tooling on PC
- requires a new playback asset format
- may require per-scene authoring exceptions in early rollout
- does not automatically solve interactive island/gameplay rendering

## Risks

### Risk 1: Plate regions are still too big

Mitigation:

- prove size on the pilot before broad rollout

### Risk 2: Compression artifacts break "pixel-perfect Johnny"

Mitigation:

- use lossless indexed storage for Johnny-containing regions
- compress transport, not image quality

### Risk 3: Ocean/background interaction is more scene-dependent than expected

Mitigation:

- allow per-scene choice:
  - runtime background
  - prerendered background-state set
  - full-frame fallback for outliers

### Risk 4: Pipeline takes too long to build

Mitigation:

- do not build the full general system first
- build the single-scene pilot first

## Recommended Product Split

If this pivot is adopted, split the game into two rendering classes:

### Class A: Offline playback scenes

- story scenes
- cutscenes
- anything whose correctness is dominated by actor composition

### Class B: Live runtime scenes

- island free movement
- menus
- pause overlay
- controller navigation
- gameplay interactions

This split is strategically healthier than insisting one renderer solve both.

## Recommendation

Adopt the pivot, but in the hybrid form only.

Do not pursue:

- naive full-screen `640x480` prerender storage

Do pursue:

- offline-authored story scene playback
- hybrid background + cropped foreground plate format
- pilot-first validation on `FISHING 1`

## Immediate Next Steps

1. Build a PC export tool for one scene:
   - host frames
   - crop rectangles
   - delta candidates
   - size report
2. Produce a real storage estimate for `FISHING 1`
3. Decide whether ocean/island remains runtime or becomes part of the playback
   asset for that scene
4. Implement a minimal PS1 playback prototype for one pilot scene
5. Compare it visually against host and current PS1
6. Only then decide whether to scale to the full `63`

## Decision Gate

Approve the pivot only if the pilot proves:

- Johnny is exact
- runtime is much simpler
- projected full-scene storage is acceptable
- pause/skip/audio integration becomes easier, not harder

If those are true, this is a more viable path than continuing the current
runtime-composition effort indefinitely.
