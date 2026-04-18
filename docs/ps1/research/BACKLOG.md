# PS1 Offline-Surface Research Backlog

Date: 2026-03-17

This backlog is designed for parallel execution by multiple agents.

Use [CURRENT_STATUS_2026-03-21.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/CURRENT_STATUS_2026-03-21.md)
for the active rollout picture and [generated/README.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/README.md)
for bulk generated artifacts. This file is a work-queue reference, not the
authoritative rollout-status snapshot.

## 2026-04-16 priority update

Immediate top priority is no longer generic pack bring-up by itself. It is:

1. keep the current prerender pilot baseline stable
2. restore the real ocean layer under that working foreground path
3. restore island after ocean
4. restore random island placement after static island

Use [OCEAN_RESTORE_PLAN_2026-04-16.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/OCEAN_RESTORE_PLAN_2026-04-16.md)
for the short-horizon step sequence and
[CURRENT_STATUS_2026-04-16.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/CURRENT_STATUS_2026-04-16.md)
for the compact current snapshot.

## How To Read This File

- Use this when choosing the next workstream or parallelizing effort.
- Use [IMPLEMENTATION_PLAN.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/IMPLEMENTATION_PLAN.md)
  for sequence and architecture.
- Use [README.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/README.md)
  as the front door.

## Track 1: Analyzer v2

Goal:
Turn the existing scene analyzer into a machine-readable build input for offline
compilation decisions.

Current first consumer:

- [scripts/plan-scene-packs.py](/home/hunter/workspace/jc_reborn/scripts/plan-scene-packs.py)
- [scene_pack_plan_2026-03-17.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/scene_pack_plan_2026-03-17.json)
- [scene_pack_manifests_2026-03-17/](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/scene_pack_manifests_2026-03-17)

Deliverables:

- `--json` or `--csv` output mode for:
  - per-scene resource sets
  - thread maxima
  - sprite frame counts
  - peak memory estimate
  - scene transitions
- PS1-accurate size accounting instead of host-dependent pointer assumptions
- emitted "enter / steady / exit" residency envelopes

Suggested starting points:

- [scene_analyzer.c](/home/hunter/workspace/jc_reborn/scene_analyzer.c)
- [scripts/analyze-scenes.sh](/home/hunter/workspace/jc_reborn/scripts/analyze-scenes.sh)
- [scene_analysis_output_2026-03-17.txt](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/scene_analysis_output_2026-03-17.txt)

## Track 2: Sprite transcoder prototype

Goal:
Compile one BMP family into a PS1-native blit format that minimizes runtime logic.

Good pilot families:

- `JOHNWALK.BMP`
- `MJFIRE` related BMPs
- `MJCOCO.BMP`

Deliverables:

- offline converter spec
- on-disk format for transcoded frames
- compression comparison:
  - raw packed indexed
  - span table
  - RLE spans
  - pre-flipped variants
- runtime decoder/blitter cost estimate

Success criteria:

- one scene family rendered without replay heuristics
- visual equivalence against PC path for the selected family

## Track 3: SDL-Compat Lite boundary

Goal:
Define and implement the minimum PS1 runtime API needed to preserve PC semantics.

Deliverables:

- exact runtime surface contract
- mapping of current engine calls to that contract
- gap list for PS1 implementation
- removal plan for gameplay-visible replay state

Minimum behaviors to preserve:

- persistent per-thread layers
- deterministic present order
- transparent blit semantics
- working save/restore zone behavior

Suggested starting points:

- [graphics.c](/home/hunter/workspace/jc_reborn/graphics.c)
- [graphics.h](/home/hunter/workspace/jc_reborn/graphics.h)
- [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c)
- [graphics_ps1.h](/home/hunter/workspace/jc_reborn/graphics_ps1.h)

## Track 4: Compiled scene-pack format

Goal:
Design a disc-first pack format that replaces ad hoc runtime asset assembly.

Deliverables:

- pack granularity decision:
  - per ADS tag
  - per scene cluster
  - hybrid
- binary layout spec
- pack manifest schema
- CD layout strategy
- transition and prefetch metadata fields
- post-processing tool for analyzer-derived transition/prefetch planning output

Pack contents should likely include:

- resolved TTM set
- sprite banks
- background references
- thread maxima
- residency envelope
- likely next-pack links

Useful current artifacts:

- [scene-transition-prefetch-report.py](/home/hunter/workspace/jc_reborn/scripts/scene-transition-prefetch-report.py)
- [scene_transition_prefetch_report_2026-03-17.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/scene_transition_prefetch_report_2026-03-17.json)
- [TRANSITION_PREFETCH_SCHEMA.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/TRANSITION_PREFETCH_SCHEMA.md)

## Track 5: Streaming and CD layout study

Goal:
Use disc layout deliberately instead of treating it as a passive asset bucket.

Deliverables:

- read-budget estimate per scene transition
- acceptable prefetch window
- contiguous file ordering recommendations
- fallback strategy when a pack misses prefetch

Questions to answer:

- how much can be read during walk transitions
- whether one or more packs should be colocated physically
- whether selected assets should be duplicated on disc to reduce seeks

## Track 6: Dirty-region and restore model

Goal:
Replace replay-recovery logic with explicit frame restoration rules where possible.

Deliverables:

- list of scene families that can use precomputed dirty regions
- offline extractor for candidate dirty rectangles
- runtime fallback rules for dynamic overlap cases

Best targets:

- walking
- fire-building / fire-blowing
- fishing
- coconut scenes

## Track 7: Validation harness

Goal:
Make future architecture experiments measurable.

Deliverables:

- screenshot comparison harness for selected scenes
- telemetry rows tied to the new runtime model
- deterministic reference runs for:
  - walking transitions
  - fire scene
  - fish scene
  - coconut scene

Reuse:

- [scripts/decode-ps1-bars.py](/home/hunter/workspace/jc_reborn/scripts/decode-ps1-bars.py)
- [docs/PS1_LEFT_DEBUG_PANEL.md](/home/hunter/workspace/jc_reborn/docs/PS1_LEFT_DEBUG_PANEL.md)
- [docs/PS1_ROOT_CAUSE_EXEC_PLAN.md](/home/hunter/workspace/jc_reborn/docs/PS1_ROOT_CAUSE_EXEC_PLAN.md)

## Recommended execution order

1. Analyzer v2
2. SDL-Compat Lite boundary
3. Sprite transcoder prototype
4. Compiled scene-pack format
5. Streaming and CD layout study
6. Dirty-region model
7. Validation harness

## Decision rules

Choose the path that removes runtime correctness heuristics, not the path that only
makes them slightly smarter.

Prefer:

- offline determinism over runtime discovery
- fixed pack formats over generic resource assembly
- bounded caches over global accumulation
- scene-specific compiled data over format-agnostic loaders

Avoid:

- new gameplay-visible replay logic
- more actor identity heuristics
- making ADS/TTM execution depend on rendering recovery state
