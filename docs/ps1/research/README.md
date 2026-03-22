# PS1 Rendering Simplification Research

Date: 2026-03-17

## Goal

Replace the current PS1-specific replay/compositing workaround model with a simpler,
more deterministic rendering architecture that uses offline preprocessing and ISO
space to recover SDL-like behavior within PS1 RAM limits.

This research package is intentionally biased toward simplification. It does not
argue that the current runtime-heavy approach is the right long-term model.

## Executive summary

Use these in order:

- [CURRENT_STATUS_2026-03-21.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/CURRENT_STATUS_2026-03-21.md)
- [CURRENT_STATUS_2026-03-21.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/CURRENT_STATUS_2026-03-21.json)
- [IMPLEMENTATION_PLAN.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/IMPLEMENTATION_PLAN.md)
- [generated/README.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/README.md)
- [archive/README.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/archive/README.md)

As of `2026-03-21`, the checked-in runtime header carries `26` promoted scene
tags, of which `25 / 63` are currently counted as verified:

- `STAND.ADS`: tags `1-12`, `15-16`
- `JOHNNY.ADS`: tags `1-6`
- `WALKSTUF.ADS`: tags `1-3`
- `MISCGAG.ADS`: tags `1-2`
- `ACTIVITY.ADS tag 4` is live in bring-up but is not yet counted as verified
  because it still leaves a stale extra-Johnny frame during the palm-climb
  route

The old one-off pilot reports and the old `2026-03-19` rollout manifest have
been moved under [archive](/home/hunter/workspace/jc_reborn/docs/ps1/research/archive)
so day-to-day work stops mixing historical milestones with the current rollout.

## What Each File Is For

- [CURRENT_STATUS_2026-03-21.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/CURRENT_STATUS_2026-03-21.md)
  Active rollout counts and what is actually verified right now.
- [IMPLEMENTATION_PLAN.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/IMPLEMENTATION_PLAN.md)
  The execution plan and architectural direction.
- [BACKLOG.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/BACKLOG.md)
  Parallelizable work tracks and queue ideas.
- [SDL_COMPAT_LITE_SPEC.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/SDL_COMPAT_LITE_SPEC.md)
  The runtime contract target.
- [generated/README.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/README.md)
  Bulk generated datasets and intermediate outputs.
- [ROLLOUT_HISTORY.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/ROLLOUT_HISTORY.md)
  The dated implementation diary.
- [archive/README.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/archive/README.md)
  Superseded snapshots and one-off artifacts.

## Directory Layout

- Root `research/`
  Human-facing reference docs, status snapshots, schemas, and the plan.
- [generated/](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated)
  Bulk generated datasets, manifests, pack outputs, and restore-spec batches.
- [archive/](/home/hunter/workspace/jc_reborn/docs/ps1/research/archive)
  Superseded snapshots and one-off pilot artifacts kept only for history.

## Key Technical References

- [SDL_COMPAT_LITE_SPEC.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/SDL_COMPAT_LITE_SPEC.md)
- [PACK_MANIFEST_SCHEMA.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/PACK_MANIFEST_SCHEMA.md)
- [PACK_PAYLOAD_LAYOUT.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/PACK_PAYLOAD_LAYOUT.md)
- [TRANSITION_PREFETCH_SCHEMA.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/TRANSITION_PREFETCH_SCHEMA.md)
- [DIRTY_REGION_TEMPLATE_SCHEMA.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/DIRTY_REGION_TEMPLATE_SCHEMA.md)

The analyzer data and current code suggest that the core problem is not that the
content fundamentally does not fit in RAM. The current static estimator says:

- Heaviest scene: `MARY.ADS tag 1 = 555.3 KB`
- Budget violations: `0 / 63 scenes`
- Maximum concurrent threads: `20`

Primary sources for those numbers:

- [scene_analyzer.c](/home/hunter/workspace/jc_reborn/scene_analyzer.c)
- [scene_analysis_output_2026-03-17.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/scene_analysis_output_2026-03-17.json)
- [scene_pack_plan_2026-03-17.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/scene_pack_plan_2026-03-17.json)

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


## Rollout History

The dated rollout chronology and implementation diary have been moved to:

- [ROLLOUT_HISTORY.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/ROLLOUT_HISTORY.md)

That file is historical context only. Use the current status snapshot and plan as
active references.
