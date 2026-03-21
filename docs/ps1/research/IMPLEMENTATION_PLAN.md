# PS1 Offline-Surface Implementation Plan

Date: 2026-03-17
Status: In progress
Owner: PS1 port refactor

## Execution status

Phase 1 has started.

Completed on 2026-03-17:

- `scene_analyzer` now supports `--json` output while preserving the text report
- peak-memory accounting now uses explicit PS1-sized pointer overhead instead of
  host `sizeof(void *)`
- derived JSON outputs exist for:
  - candidate scene clusters
  - shared resource inventories
  - transition churn ranking
  - first-pass prefetch heuristics
- analyzer JSON now has a post-processing path that emits:
  - pack candidates
  - adjacent transition edges
  - ranked prefetch edges
  - pack-boundary candidates
- generated artifact:
  [scene_analysis_output_2026-03-17.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/scene_analysis_output_2026-03-17.json)
- draft Phase 5 manifest contract:
  [PACK_MANIFEST_SCHEMA.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/PACK_MANIFEST_SCHEMA.md)
- draft pack payload layout:
  [PACK_PAYLOAD_LAYOUT.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/PACK_PAYLOAD_LAYOUT.md)
- derived artifact:
  [scene_transition_prefetch_report_2026-03-17.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/scene_transition_prefetch_report_2026-03-17.json)
- first pack-planning consumer:
  [scripts/plan-scene-packs.py](/home/hunter/workspace/jc_reborn/scripts/plan-scene-packs.py)
- generated pack-planning artifacts:
  [scene_pack_plan_2026-03-17.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/scene_pack_plan_2026-03-17.json)
  and
  [scene_pack_manifests_2026-03-17/](/home/hunter/workspace/jc_reborn/docs/ps1/research/scene_pack_manifests_2026-03-17)
- compiled pack artifacts:
  [compiled_packs_2026-03-17](/home/hunter/workspace/jc_reborn/docs/ps1/research/compiled_packs_2026-03-17)
- staged CD payloads:
  [jc_resources/packs](/home/hunter/workspace/jc_reborn/jc_resources/packs)
- binary table-of-contents now embedded directly in each staged `.PAK`
- generic runtime loader hook:
  [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c)

Still pending inside Phase 1:

- tighten heuristics into validated transition data where available
- validate the binary table-of-contents path under real scene traversal and then
  decide whether to keep `pack_index.json` as a research/debug sidecar only
- continue shrinking fallback so only genuinely dynamic overlap cases remain;
  current status: `ADS/SCR/TTM/BMP` scene assets are now pack-authoritative once
  a family pack is active, and the current bounded validation path decodes
  `pilot_pack ... fallbacks=0`
- keep baseline progression on the normal boot path healthy while scene-scoped
  rollout continues; current status: the first island handoff no longer
  collapses to black after `adsPlayWalk()` now resets `ttmSlots[0]`, and the
  longer `story phase=4` window on that route is understood as the walk/takeover
  still running rather than a frozen story-state counter

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

Current status:

- the first working contract and gap matrix now live in
  [SDL_COMPAT_LITE_SPEC.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/SDL_COMPAT_LITE_SPEC.md)
- PS1 `SAVE_ZONE/RESTORE_ZONE` is no longer stubbed for the simple active-zone
  script pattern
- PS1 `COPY_ZONE_TO_BG` / `SAVE_IMAGE1` now commit bounded rectangles into the
  clean background restore baseline instead of remaining stubs
- the main remaining leaks are replay continuity and `CLEAR_SCREEN` divergence

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

Current status:

- PS1 now has a real `grSaveZone()` / `grRestoreZone()` implementation in the
  pilot runtime path, restoring bounded rectangles from clean background tiles
  instead of leaving `RESTORE_ZONE` stubbed out
- pack-backed scene traversal still validates with `pilot_pack ... fallbacks=0`
  after this restore-path change
- an offline extractor now emits first-pass dirty-region template candidates
  from pack manifests plus extracted `TTM` bytecode, without changing the live
  runtime path
- a first runtime `CLEAR_SCREEN` consumer was tried on `BUILDING.ADS` and then
  backed out after later black-background regressions; current Phase 7 progress
  remains offline/template-side until restore policy is tied to validated scene
  state
- the current scene-level restore pilot target is the compatible
  `STAND.ADS tags 1-3` cluster (`scene_index 38` origin), selected from the
  restore-candidate report with a `352x140` union rect owned by
  `MJAMBWLK.TTM` and `MJTELE.TTM`
- the scene-level pilots now have a generated runtime-facing table
  (`ps1_restore_pilots.h`) derived from the offline pilot specs
- the first scene-scoped runtime consumer now exists in `ttm.c`: on
  the `STAND.ADS tags 1-3` pilot cluster, `CLEAR_SCREEN` can restore only the
  matching pilot-TTM rect for `MJAMBWLK.TTM` / `MJTELE.TTM`, rather than
  applying a family-wide hook
- that same scene-scoped pilot now tracks `TTM_UNKNOWN_1` region ids per thread
  and extends the generated-contract hook to `SAVE_IMAGE1` for the same
  `STAND.ADS` pilot cluster (`tags 1-3`) / `MJAMBWLK.TTM` / `MJTELE.TTM` route
- the PS1 validation harness now captures a later three-frame series for forced
  scene boots, which matches the real post-title boot timing of the `STAND`
  pilot path and makes that route repeatable to validate
- on that same `STAND.ADS tags 1-3` route, `ads.c` now disables replay merge,
  actor recovery, and handoff carry/injection so the pilot depends on the new
  restore contract rather than legacy replay continuity
- that same scene-scoped pilot path is now generalized enough to cover
  `JOHNNY.ADS tag 1` through the generated pilot table, and a forced
  `JOHNNY.ADS 1` run still validates with `pilot_pack ... fallbacks=0`
- the black-backed `MEANWHIL` clock card on the `JOHNNY` route was traced to
  authored scene behavior in `MEANWHIL.TTM` (`SET_COLORS 5 5`,
  `DRAW_RECT 0 0 640 350`, then repeated clock sprite draws), not to the new
  restore/prefetch/pack path
- the next ranked candidate is now emitted as a checked-in pilot spec too:
  `WALKSTUF.ADS tag 2`, covering `MJJOG.TTM`, `MJRAFT.TTM`, and `WOULDBE.TTM`
  with a wider union rect and a two-region clear/save contract for `WOULDBE`
- the generated pilot specs now carry explicit scene resource lists
  (`bmps/scrs/ttms`), and the PS1 runtime primes those scene-scoped resources
  before play through `ps1_restore_pilots.h` + `ads.c`; this is the first
  offline-generated preload contract for the pilot routes
- the restore-spec generator now has a batch mode, and
  `restore_pilot_specs_2026-03-19/` contains the first five recommended
  scene-level specs (`STAND`, `JOHNNY`, `WALKSTUF`, `ACTIVITY`, `FISHING`)
- the header generator can now consume a spec directory plus ADS filters, so
  promotion from offline spec to runtime table is a selection step instead of a
  hand-maintained file list
- the candidate-report/spec pipeline now scales to the full current ADS set:
  `restore_candidate_report_full_2026-03-19.*` plus
  `restore_pilot_specs_full_2026-03-19/` emit one pre-calculated restore scene
  for all ten ADS families (`STAND`, `JOHNNY`, `WALKSTUF`, `ACTIVITY`,
  `FISHING`, `BUILDING`, `VISITOR`, `MARY`, `MISCGAG`, `SUZY`)
- the same tooling now scales beyond one-per-family pilots:
  `restore_scene_specs_full_2026-03-19/` emits `63` scene-scoped restore specs,
  one per ranked scene, so offline conversion can advance across the whole ADS
  surface without waiting for one-by-one runtime promotion
- `restore_rollout_manifest_2026-03-19.*` classifies that full scene batch into
  `live_proven` (`5` scenes), `offline_ready` (`56` scenes), and
  `blocked_entry_path` (`2` scenes)
- `restore_scene_clusters_2026-03-19.*` compresses those `63` scenes into `34`
  shared restore contracts; this is now the right promotion unit for runtime
  enablement instead of single scenes or hand-picked family labels
- `restore_cluster_specs_2026-03-19/` now materializes those `34` grouped
  contracts as reusable cluster specs, so runtime promotion can consume the
  same generated-contract path we already use for pilot scenes
- the header generator now treats missing per-TTM rects as disabled hook rows
  instead of failing generation, so research-grade candidates can stay in the
  offline queue before their every TTM contract is runtime-ready
- fallback telemetry is now real instead of stale: counters reset on pack
  activation and increment only on extracted-file fallback reads
- `STAND.ADS` still validates with `pilot_pack ... fallbacks=0` after that
  correction
- `WALKSTUF.ADS 2` no longer drops Johnny on the opening board frame: the
  scene-scoped pilot now keeps replay merge suppressed but re-enables only
  one-frame missing-actor recovery for that route, which restores the opening
  Johnny pose without broadening replay carry policy again
- `WALKSTUF.ADS 2` still shows one real active-pack miss (`WOULDBE.BMP`
  signature `17`) even though `WALKSTUF.PAK` contains the expected assets; that
  remaining gap is now a secondary runtime pack-read cleanup, not the visible
  first-frame regression
- the forced-scene harness now supports `story ads <ADS> <tag>` and routes that
  request through the normal `storyPlay()` final-scene flow instead of a custom
  boot shim, which is the right long-term shape for validating scene-scoped
  pilots that need real story/island setup
- `ACTIVITY.ADS tag 4` remains blocked as a live pilot even with that corrected
  story path: the pack activates, but the forced route still does not reach a
  valid composed scene, so it stays offline-only for now
- the runtime header has been re-tightened to the proven set only
  (`STAND`, `JOHNNY`, `WALKSTUF`) so the tree cleanly distinguishes
  scene-scoped offline conversion from scene-scoped live enablement
- the offline extractor now interprets TTM rect origins as signed coordinates
  and clamps them to the visible scene bounds before clustering; regenerating
  the full `63`-scene / `34`-cluster artifact set fixed wrapped `VISITOR`
  envelopes such as `width=65551` and makes that family a sane next rollout
  target once entry validation is ready
- the first contract-sized runtime expansion is now in place: the live header
  consumes the shared `STAND` cluster contract for tags `1-12`, and bounded
  forced validation of `STAND.ADS 4` plus `STAND.ADS 12` stayed visually good
- the second contract-sized runtime expansion is now in place too: the live
  header retains the proven `JOHNNY.ADS 1` singleton and adds the shared
  `JOHNNY.ADS 2-5` contract, with bounded forced validation of
  `JOHNNY.ADS 2` plus `JOHNNY.ADS 5` staying visually good
- that grouped rollout now extends one step further: the live header also
  carries the validated `JOHNNY.ADS 6` singleton contract, and a bounded forced
  `JOHNNY.ADS 6` run stayed visually good
- `STAND` has now been widened past the first shared contract too: the live
  header adds the second `STAND.ADS 15-16` cluster, and bounded forced runs of
  `STAND.ADS 15` plus `STAND.ADS 16` stayed visually good
- `BUILDING` was attempted as the next grouped rollout target, but current
  `island ads` and `story ads` entry paths still land on bootstrap/ocean
  states instead of valid composed scenes, so `BUILDING` remains offline-only
  until its entry path is reproducible
- `WALKSTUF` has now been widened in the live header from the original tag `2`
  singleton to tags `1-3`; bounded forced runs of `WALKSTUF.ADS 1` and
  `WALKSTUF.ADS 3` stayed visually good, but both still show active-pack
  fallbacks, so that family is now a valid restore-pilot route with remaining
  pack cleanup debt rather than a restore-policy blocker

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
