# PS1 Offline-Surface Implementation Plan

Date: 2026-03-17
Status: In progress
Owner: PS1 port refactor

Reference navigation:

- [CURRENT_STATUS_2026-03-21.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/CURRENT_STATUS_2026-03-21.md)
  Active rollout snapshot.
- [CURRENT_STATUS_2026-04-16.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/CURRENT_STATUS_2026-04-16.md)
  Current prerender pilot snapshot.
- [OCEAN_RESTORE_PLAN_2026-04-16.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/OCEAN_RESTORE_PLAN_2026-04-16.md)
  Active short-term ocean reintegration plan.
- [generated/README.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/README.md)
  Bulk generated datasets and manifests.
- [archive/README.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/archive/README.md)
  Historical snapshots only.

## 2026-04-16 update

Current project-level reality:

- the prerender pilot is now real, not hypothetical
- `fishing1` is visually correct, full-scene, and near-PC timing on PS1
- the generic foreground runtime path has already been proven on a second scene
  (`fishing2`)
- the immediate product-facing blocker is no longer timing; it is restoring the
  real ocean/island background under the working prerender foreground path

Important new constraint:

- naive background swaps inside the standalone prerender loop are not safe
- full `adsInitIsland() + adsPlay()` routing is also not a safe substitute for
  `fgpilot`

That means the short-term execution path is:

1. keep the standalone prerender player as the foreground baseline
2. reintroduce the real ocean layer through a background-only path
3. only after ocean is stable, restore island
4. only after static island is stable, restore random island placement

This does not replace the long-term offline-scene architecture. It sharpens the
next milestone needed to make that architecture look like a real game again.

## Execution status

Current active state:

- offline analyzer, pack planning, pack compilation, restore-spec generation,
  and clustering are all in place
- the active rollout count is tracked in
  [CURRENT_STATUS_2026-03-21.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/CURRENT_STATUS_2026-03-21.md)
- scene assets are pack-authoritative once a family pack is active, and the
  current bounded validation path decodes `pilot_pack ... fallbacks=0` on the
  clean routes
- the renderer now uses dirty-row background restore/upload and other hot-path
  optimizations in [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c),
  so renderer correctness must now be validated at the dirty-region boundary
- offline-transcoded `PSB` sprite bundles are part of the live pack/runtime
  architecture, with targeted BMP exceptions only where PSB parity is not yet
  proven
- baseline normal boot no longer collapses on the first island handoff after
  `adsPlayWalk()` now resets `ttmSlots[0]`

Key supporting artifacts:

- analyzer output:
  [scene_analysis_output_2026-03-21.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/scene_analysis_output_2026-03-21.json)
- pack plan:
  [scene_pack_plan_2026-03-21.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/scene_pack_plan_2026-03-21.json)
- pack manifests:
  [scene_pack_manifests_2026-03-21/](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/scene_pack_manifests_2026-03-21)
- compiled research packs:
  [compiled_packs_2026-03-21](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/compiled_packs_2026-03-21)
- transition/prefetch report:
  [scene_transition_prefetch_report_2026-03-21.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/scene_transition_prefetch_report_2026-03-21.json)

Near-term open work:

- keep the current prerender pilot baseline stable
- restore the real ocean layer under prerender playback without changing
  foreground playback semantics
- restore island only after ocean is proven stable
- then preserve normal boot and story-scene handoffs while the live restore set
  expands

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
- rendering mismatch documentation:
  [README.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/README.md)
- existing research backlog:
  [BACKLOG.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/BACKLOG.md)
- current analyzer text snapshot:
  [scene_analysis_output_2026-03-21.txt](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/scene_analysis_output_2026-03-21.txt)

The key conclusion so far is:

- the content appears statically manageable inside the current estimated RAM budget
- the dominant problem is semantic mismatch between PC SDL layering and the current
  PS1 replay/composite model

That means the execution strategy should focus on replacing the boundary, not merely
patching the symptoms.

## Why This Should Fix The Disappearing-Johnny Bugs

The long-running "Johnny disappears" class is no longer a vague symptom bucket.
The work done so far has narrowed it to a small number of concrete failure modes:

- stale slot/state carry across scene or walk handoff
- replay continuity being used as a correctness mechanism instead of as legacy
  glue
- route-specific pack-read tails on hot actor assets such as `JOHNWALK`
- sprite-path divergence between offline-transcoded `PSB` loading and the older
  BMP path
- renderer dirty-region mistakes that can leave stale rows or fail to restore a
  touched region correctly
- restore/clear policy gaps that leave one-frame ghosts or missing actor pieces

The architecture direction in this plan is aimed directly at those failures:

- compiled scene contracts replace implicit runtime discovery
- pack-authoritative scene loading removes extracted-file drift
- per-scene restore policy replaces replay-era recovery heuristics
- verification gates force visual proof before promotion
- family completion means removing the old correctness dependencies instead of
  merely adding more special cases

If the plan is followed to completion, Johnny-disappearing bugs should stop
moving around because the runtime will no longer depend on the same unstable
state-carry and replay-resurrection paths that have been causing them.

## Target end-state

The intended final architecture is:

1. Analyzer v2 emits machine-readable scene/resource/transition data.
2. Build pipeline compiles selected assets into PS1-native formats.
3. Scene packs group assets and metadata for deterministic runtime use.
4. PS1 runtime implements a narrow `SDL-Compat Lite` surface contract.
5. Gameplay code stops depending on replay-record continuity logic for correctness.

## Conversion And Verification Program

This is the active step-by-step program for finishing the port from the current
state. The first-order priority is to complete the offline artifact surface
before trying to finish every runtime route, because that removes large classes
of uncertainty and lets the remaining work be driven by explicit contracts.

### Stage A: Complete The Artifact Surface First

#### Step A1: Freeze And Protect The Baseline

Goal:
Normal boot and canonical story handoffs must stay alive while artifact work
continues.

Required outcomes:

- no first-scene or first-handoff blackscreen regressions
- no stale boot-override leakage from the harness
- normal boot remains a trusted certification path
- no dirty-row renderer regressions that leave stale or un-restored frame
  regions during normal story progression

Why this matters:

- artifact completion is only useful if the baseline stays reproducible while
  the generated surface grows

#### Step A2: Complete Scene Coverage Artifacts

Goal:
Every scene should have a current, machine-readable offline record.

Required outcomes:

- analyzer output current and trustworthy
- one scene-scoped restore spec per reachable scene
- one cluster/spec view for grouped promotion
- clear status for verified, bring-up, blocked, and unpromoted scenes

Bug classes eliminated once complete:

- missing-scene planning ambiguity
- "we do not know what resources/rects this route needs"
- contract gaps caused purely by absent generated data

Bug classes not eliminated yet:

- runtime slot/state carry bugs
- renderer/path bugs

#### Step A3: Complete Pack/Manifest Artifacts

Goal:
Every target family should have a coherent offline pack contract.

Required outcomes:

- pack manifests complete and current
- compiled research packs regenerated from current inputs
- payload/index sidecars internally consistent
- transition/prefetch planning outputs regenerated from the current surface

Current `2026-03-21` Stage A3 status:

- analyzer snapshot refreshed under `generated/scene_analysis_output_2026-03-21.*`
- pack plan/manifests refreshed under `generated/scene_pack_plan_2026-03-21.json`
  and `generated/scene_pack_manifests_2026-03-21/`
- dirty-region templates refreshed under
  `generated/dirty_region_templates_2026-03-21/`
- compiled research packs refreshed under
  `generated/compiled_packs_2026-03-21/`
- transition/prefetch outputs refreshed under
  `generated/scene_transition_prefetch_report_2026-03-21.*`

Bug classes eliminated once complete:

- uncertainty about whether a family was omitted from the pack pipeline
- stale manifest/layout mismatches caused by outdated generated outputs
- confusion about which assets should be in-disc vs dynamic overlap

Bug classes not eliminated yet:

- route-specific runtime read failures
- PSB/BMP load-path divergence
- dirty-row restore/upload correctness bugs

#### Step A4: Complete Promotion-Readiness Artifacts

Goal:
Promotion decisions should be mechanically derived from artifacts, not from
memory of past experiments.

Required outcomes:

- explicit per-scene/per-cluster readiness labels
- explicit blocked-entry-path labels
- explicit current/live/verified snapshot
- docs and generated outputs agree on counts

Bug classes eliminated once complete:

- promoting the wrong route because docs disagree
- wasting time validating routes whose offline contract is not actually ready

#### Step A5: Lock The Artifact Boundary

Goal:
Finish the offline surface enough that later runtime bugs can no longer be
blamed on missing artifact work by default.

Definition of artifact-complete:

- all intended scene families have current manifests/specs/clusters
- generated docs/snapshots are internally consistent
- blocked routes are blocked because of entry/runtime behavior, not because
  offline artifacts are absent

Why this matters:

- once this stage is done, an entire class of "maybe the compiler/manifests are
  incomplete" explanations can be ruled out first

### Stage B: Runtime Conversion On Top Of A Finished Artifact Surface

#### Step B1: Finish The Current Bring-Up Route

Goal:
Close the remaining `ACTIVITY.ADS tag 4` stale-frame bug and move it from
bring-up to verified.

Required outcomes:

- remove the stale extra-Johnny climb frame
- keep Johnny present/intact through the whole route
- keep pack-authoritative behavior with no new fallback dependence

Why this matters:

- it proves the plan can take a still-buggy route all the way to verified

#### Step B2: Close Remaining Tail Bugs On Live Families

Goal:
Drive the current promoted families from "visually valid" to "clean and
deterministic."

Current known tails:

- `MISCGAG.ADS 2`: route-specific `JOHNWALK` pack/runtime tail
- `JOHNNY` edge routes: validate remaining PSB/BMP-path consistency
- `WALKSTUF`: remaining pack-read cleanup on the hot actor route

Required outcomes:

- identify whether each remaining tail is:
  - pack read
  - slot retention
  - PSB/BMP divergence
  - restore/clear artifact
- fix it at that boundary, not with broader preload heuristics

#### Step B3: Promote By Shared Contract, Not One Scene At A Time

Goal:
Use generated cluster specs as the rollout unit wherever entry paths are
reproducible.

Required outcomes:

- promote clean clusters once edge scenes are validated
- avoid one-off scene hooks when an existing shared contract already covers the
  route
- keep the live header aligned with actually validated contracts

Success condition:

- rollout effort scales by contract/family slice, not by all `63` scenes

#### Step B4: Fix Blocked Entry Paths

Goal:
Turn currently blocked families into valid certification targets.

Current blocked/unreliable families:

- `BUILDING.ADS`
- `FISHING.ADS`

Required outcomes:

- trustworthy `story scene <index>` or equivalent normal-context entry path
- visual proof that the route is genuinely composed, not just "pack active"

#### Step B5: Remove Replay As A Correctness Dependency Family By Family

Goal:
Once a family is verified and clean, stop using replay merge/carry/recovery as
its correctness mechanism.

Required outcomes:

- scene-scoped restore policy provides the needed behavior
- actor recovery stays only where it is still explicitly justified
- no new family-wide heuristics are added

Definition of done for a family:

- verified entry path
- no disappearing/ghosting actor bug on the canonical route
- no unexplained active-pack fallback dependence
- replay continuity is no longer required for correctness on that family

#### Step B6: Reconcile PSB And BMP Hot Paths

Goal:
Finish the sprite-path simplification so hot actor assets do not depend on
route-specific bypasses forever.

Required outcomes:

- prove whether `JOHNWALK` should stay on BMP temporarily or move back to PSB
- remove temporary path divergences once visual behavior matches
- keep the pack/compiler/runtime contract aligned with the chosen sprite path

#### Step B7: Make Verification Artifacts The Promotion Truth

Goal:
Promotion should be driven by explicit evidence, not memory of old runs.

Required outcomes:

- current status snapshot stays accurate
- live header, verified counts, and docs stay in sync
- screenshots/telemetry for promotion edges are easy to find

#### Step B8: Endgame Cleanup

Goal:
Once enough families are complete, simplify the runtime instead of preserving
legacy branches indefinitely.

Required outcomes:

- remove or isolate family paths that still depend on old replay correctness
- collapse temporary bring-up exceptions
- keep only the SDL-lite contract, pack loader, and explicit restore policy
  machinery that the finished port still needs

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

- `CURRENT_STATUS_2026-03-21.*` is the active rollout snapshot; the chronology
  below is preserved for implementation history and rationale, not as the
  authoritative current scene-count report
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
- `CURRENT_STATUS_2026-03-21.*` is now the active rollout snapshot for
  day-to-day work: `25 / 63` scenes are currently verified, `26` scene tags are
  live in the generated header, and `ACTIVITY.ADS tag 4` is the current
  bring-up route but is not yet counted as verified
- the original `restore_rollout_manifest_2026-03-19.*` snapshot has been moved
  under `docs/ps1/research/archive/2026-03-19-rollout-snapshot/` because it
  still reports the older `5 live_proven` milestone and was starting to confuse
  current planning
- `restore_rollout_manifest_2026-03-21.*` is now the current generated
  readiness manifest derived from the active status snapshot, with four clear
  buckets: `verified_live`, `live_bringup`, `artifact_ready_unverified`, and
  `blocked_entry_path_or_unreliable_route`
- `restore_scene_clusters_2026-03-21.*` compresses those `63` scenes into `33`
  shared restore contracts under the current status model; this is now the
  right promotion unit for runtime enablement instead of single scenes or
  hand-picked family labels
- `restore_cluster_specs_2026-03-21/` now materializes those `33` grouped
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
- historical note: at this point in the rollout, `ACTIVITY.ADS tag 4` was still
  blocked as a live pilot and the runtime header had been re-tightened to the
  then-proven set only (`STAND`, `JOHNNY`, `WALKSTUF`)
- current note: the header now also carries `MISCGAG.ADS 1-2` as verified and
  `ACTIVITY.ADS 4` as an active bring-up route, while the verified count
  remains `25 / 63` until `ACTIVITY` stops leaving a stale extra-Johnny climb
  frame
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
