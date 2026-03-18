# PS1 Offline-Surface Implementation Plan

Date: 2026-03-17
Status: Proposed execution plan
Owner: PS1 port refactor

## Objective

Replace the current PS1 replay/recovery rendering model with a deterministic,
offline-authored rendering architecture that:

- preserves the engine's SDL-era semantics closely enough that gameplay code does
  not need PS1-specific recovery logic
- uses offline analysis and ISO space to reduce runtime complexity
- keeps RAM bounded and predictable on PS1
- is easier to validate than the current heuristic-heavy pipeline

This plan assumes the existing "fix the current runtime" work may continue in
parallel for short-term stability, but that the long-term architecture work should
move in a different direction.

## Non-goals

This plan is not trying to:

- make the current replay heuristics incrementally smarter forever
- preserve the existing PS1 rendering internals as an architectural constraint
- optimize for the fewest changed files in the short term
- produce a perfect universal pack/streaming system before proving the idea on one
  pilot path

## Guiding principles

1. Prefer offline determinism over runtime discovery.
2. Prefer explicit contracts over "best effort" equivalence.
3. Prefer scene-specific compiled data over generic runtime interpretation.
4. Prefer bounded caches over open-ended accumulation.
5. Prefer validation harnesses and measurable gates over anecdotal confidence.

## Current diagnosis

The project already has several strong foundations:

- static scene analysis:
  [scene_analyzer.c](/home/hunter/workspace/jc_reborn/scene_analyzer.c)
- dated analyzer artifact:
  [scene_analysis_output_2026-03-17.txt](/home/hunter/workspace/jc_reborn/docs/ps1/research/scene_analysis_output_2026-03-17.txt)
- rendering mismatch documentation:
  [README.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/README.md)
- existing research backlog:
  [BACKLOG.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/BACKLOG.md)

The key conclusion so far is:

- the content appears statically manageable inside the current estimated RAM budget
- the dominant problem is semantic mismatch between PC SDL layering and the current
  PS1 replay/composite model

That means the execution strategy should focus on replacing the boundary, not merely
patching the symptoms.

## Target end-state

The intended final architecture is:

1. Analyzer v2 emits machine-readable scene/resource/transition data.
2. Build pipeline compiles selected assets into PS1-native formats.
3. Scene packs group assets and metadata for deterministic runtime use.
4. PS1 runtime implements a narrow `SDL-Compat Lite` surface contract.
5. Gameplay code stops depending on replay-record continuity logic for correctness.

## Phase map

The recommended program is split into eight phases.

### Phase 0: Architecture lock

Purpose:
Lock the rules of engagement before implementation starts spreading.

Tasks:

- Document the design rule that gameplay correctness must not depend on replay
  recovery, actor resurrection, or scene-handoff heuristics.
- Declare the intended long-term PS1 runtime boundary.
- Freeze the addition of new gameplay-visible recovery mechanisms unless they are
  temporary bug fixes on the legacy path.
- Decide which pilot asset family and pilot scene family will be used first.

Suggested pilot choices:

- sprite pilot: `JOHNWALK.BMP`
- scene pilot: one walk-heavy island scene plus one fire/building scene

Deliverables:

- architecture statement in research docs
- list of approved pilot targets

Exit criteria:

- all implementation tracks agree on the same target boundary

Risks:

- continuing to add legacy-path complexity while the new path is being built

### Phase 1: Analyzer v2

Purpose:
Turn static analysis into build input instead of human-only reporting.

Current base:

- [scene_analyzer.c](/home/hunter/workspace/jc_reborn/scene_analyzer.c)
- [scripts/analyze-scenes.sh](/home/hunter/workspace/jc_reborn/scripts/analyze-scenes.sh)

Tasks:

1. Add machine-readable output.
- Support `--json` first. `--csv` can be added later if useful.
- Keep the existing human-readable mode intact.

2. Make memory accounting PS1-accurate.
- Remove any host-size assumptions that vary between 32-bit and 64-bit builds.
- Emit explicit component sizes instead of implicit `sizeof`-style inference when
  possible.

3. Emit scene-level structures that downstream tools can consume.
- scene id and story metadata
- ADS tag
- BMP/SCR/TTM sets
- maximum thread count
- frame counts
- estimated peak memory
- per-slot resource bindings
- transition metadata when available

4. Add derived outputs useful to pack compilation.
- candidate scene clusters
- resources shared across scenes
- heaviest transition deltas
- likely prefetch sets

5. Add a checked-in generated artifact.
- dated JSON output in `docs/ps1/research/`

Deliverables:

- updated analyzer code
- documented output schema
- generated JSON artifact

Validation:

- human-readable mode still matches current summary
- JSON contains the same core data
- no scene count regressions

Exit criteria:

- downstream tools can consume analyzer output without scraping text

Risks:

- under-modeling transition behavior if the current analyzer remains too scene-static

### Phase 2: SDL-Compat Lite contract

Purpose:
Define the minimum runtime semantics the PS1 path must satisfy.

Current reference points:

- PC behavior:
  [graphics.c](/home/hunter/workspace/jc_reborn/graphics.c)
- PS1 behavior:
  [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c)
  [graphics_ps1.h](/home/hunter/workspace/jc_reborn/graphics_ps1.h)

Tasks:

1. Enumerate all graphics calls actually used by gameplay scripts.
- draw sprite
- draw flipped sprite
- copy/save/restore zone
- clip
- line / rect / pixel / circle where relevant
- layer acquisition and release

2. Separate "must match PC semantics" from "implementation detail."

3. Define the minimum stable API surface.
- logical layer lifecycle
- frame begin
- frame present
- sprite draw semantics
- save/restore semantics
- clip semantics

4. Create a gap matrix.
- implemented on PC
- implemented on PS1
- partial on PS1
- stubbed or fake on PS1

5. Identify where gameplay code currently depends on PS1 workarounds.
- replay state
- continuity records
- recovery injection

Deliverables:

- contract document
- gap matrix
- dependency map from gameplay/rendering code

Validation:

- every gameplay graphics call maps cleanly to the contract
- no contract requirement depends on the current replay workaround

Exit criteria:

- the team can implement the runtime against the contract rather than against ad hoc
  existing behavior

Risks:

- defining the contract too broadly and recreating SDL in full
- defining it too narrowly and discovering hidden script dependencies later

### Phase 3: Sprite transcoder prototype

Purpose:
Move sprite format complexity offline and prove that runtime can be much simpler.

Pilot target:

- `JOHNWALK.BMP`

Why:

- high reuse
- known trouble case
- crosses PS1 texture-height constraints
- directly involved in visible regressions

Tasks:

1. Write format study for one family.
- raw packed indexed
- row-span table
- RLE spans
- pre-flipped variants
- optional tile-chopped forms

2. Choose one on-disk format for the pilot.

3. Build an offline converter.
- input: extracted BMP family
- output: PS1-native pilot asset format

4. Define runtime decode/blit path for pilot format.
- deterministic
- bounded RAM
- no replay heuristics required for correctness

5. Add tooling output comparison.
- original frame counts
- encoded byte size
- estimated RAM footprint
- decode cost class

Deliverables:

- converter tool
- format spec
- pilot asset output
- runtime prototype reader/blitter

Validation:

- visual equivalence for walking frames against PC path
- no multi-painting
- no disappear/recover behavior needed on pilot path

Exit criteria:

- one high-value sprite family works through the new offline path

Risks:

- pilot format too optimized too early
- spending time on a transcoder that doesn't align with future scene packs

### Phase 4: Pilot runtime path

Purpose:
Prove the new architecture on a narrow slice before expanding scope.

Tasks:

1. Route pilot family through the new transcoded path.
2. Keep the legacy path available for everything else.
3. Implement a feature switch or explicit code path marker.
4. Remove replay continuity dependence from the pilot route.
5. Measure:
- correctness
- CPU cost
- RAM usage
- regression behavior over long runs

Deliverables:

- pilot path integrated into PS1 runtime
- instrumentation for pilot path behavior

Validation:

- repeated walk transitions remain stable
- no blinking between pilot-path destinations
- no duplicate body frames

Exit criteria:

- one path is demonstrably simpler and more stable than the legacy route

Risks:

- pilot path accidentally still leaning on legacy behavior behind the scenes

### Phase 5: Compiled scene packs

Purpose:
Replace generic runtime resource assembly with deterministic scene-oriented packs.

Tasks:

1. Choose pack granularity.
- per ADS tag
- per scene cluster
- hybrid pack + shared bank

2. Define pack contents.
- TTM references or compiled TTM metadata
- sprite banks
- residency envelopes
- transition hints
- prefetch links
- optional dirty-region templates

3. Create manifest schema.
4. Build pack compiler from analyzer output.
5. Add runtime loader for pilot pack family.

Deliverables:

- scene pack format spec
- pack compiler
- runtime loader

Validation:

- pack content matches analyzer-derived scene requirements
- pilot scene loads without generic runtime discovery

Exit criteria:

- one scene family runs primarily from compiled scene data

Risks:

- exploding the number of packs
- pack duplication becoming too large without a shared-bank model

### Phase 6: Streaming and disc layout

Purpose:
Use the disc deliberately as part of the runtime system.

Tasks:

1. Model transition-time read budget.
2. Identify low-risk prefetch windows.
3. Group physically adjacent pack files.
4. Decide whether some assets should be duplicated on disc to reduce seeks.
5. Add bounded prefetch policy for pilot packs.

Deliverables:

- disc layout recommendations
- prefetch policy
- pack ordering strategy

Validation:

- no stalls during pilot transitions
- predictable load behavior

Exit criteria:

- the pack path can rely on disc access without unstable behavior

Risks:

- assuming seek behavior is free
- overfitting to emulator behavior rather than real hardware constraints

### Phase 7: Dirty-region and restore model

Purpose:
Replace "restore by replaying remembered sprites" with "restore by explicit region
rules" wherever the content allows it.

Tasks:

1. Identify scene families with stable dirty patterns.
2. Build offline dirty-region extractor or templates.
3. Integrate restore logic into pilot packs or pilot runtime path.
4. Keep fallback path only for truly dynamic overlap cases.

Deliverables:

- dirty-region template format
- pilot scene templates
- runtime restore implementation

Validation:

- stable erase/redraw behavior in pilot scenes
- fewer runtime continuity records required

Exit criteria:

- at least one scene family restores without replay-resurrection logic

Risks:

- trying to template scenes that are too dynamic

### Phase 8: Validation harness and rollout

Purpose:
Prevent architecture work from becoming another unmeasured rewrite.

Tasks:

1. Define canonical test scenes.
- walking
- fire/building
- fishing
- coconut

2. Standardize captures.
- screenshots
- telemetry rows
- long-run stability checks

3. Add comparison workflow.
- PC reference
- legacy PS1 path
- new pilot path

4. Define rollout rules.
- promote only when simpler and more stable
- keep legacy path only where needed temporarily

Deliverables:

- harness docs
- test workflow
- promotion criteria

Validation:

- reproducible comparison runs for every architecture step

Exit criteria:

- future work can prove value instead of relying on subjective reports

## Parallel execution model

This plan is intentionally decomposed for multi-agent execution.

### Agent group A: Analysis and data

Owns:

- analyzer v2
- schema design
- generated artifacts
- pack inputs

### Agent group B: Runtime contract

Owns:

- SDL-Compat Lite specification
- gap matrix
- runtime API cut plan

### Agent group C: Offline asset pipeline

Owns:

- sprite transcoder
- pilot format
- encoding trade study

### Agent group D: Pack system

Owns:

- pack format
- pack compiler
- runtime pack loader
- disc layout strategy

### Main integration thread

Owns:

- architectural consistency
- validation
- selection of pilot paths
- merging and pruning complexity

## Success criteria

The project should treat this plan as successful only if it achieves all of the
following:

1. One pilot path renders through the new architecture without relying on gameplay-
visible replay heuristics.
2. That pilot path is simpler to reason about than the current path.
3. The analyzer output can drive build decisions directly.
4. Pack/transcoded asset generation is reproducible.
5. Validation results are repeatable and not anecdotal.

## Failure modes to watch

1. Building a second complex system beside the first one without retiring any logic.
2. Letting pilot implementations quietly depend on legacy replay behavior.
3. Spending too long perfecting pack formats before proving the first transcoder.
4. Underestimating disc-layout and transition-read constraints.
5. Treating "works in one emulator run" as architectural proof.

## Recommended immediate next steps

1. Implement `Analyzer v2` JSON output.
2. Write the `SDL-Compat Lite` contract and gap matrix.
3. Start the `JOHNWALK` transcoder pilot.

That is the shortest path to proving the new architecture on real content.
