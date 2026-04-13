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
- [OFFLINE_SCENE_PLAYBACK_PIVOT_2026-04-12.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/OFFLINE_SCENE_PLAYBACK_PIVOT_2026-04-12.md)
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
The current generated readiness/cluster surface now lives under:

- [generated/restore_rollout_manifest_2026-03-21.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/restore_rollout_manifest_2026-03-21.md)
- [generated/restore_scene_clusters_2026-03-21.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/restore_scene_clusters_2026-03-21.md)
- [generated/restore_cluster_specs_2026-03-21](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/restore_cluster_specs_2026-03-21)

The current pack-side artifact surface now lives under:

- [generated/scene_analysis_output_2026-03-21.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/scene_analysis_output_2026-03-21.json)
- [generated/scene_pack_plan_2026-03-21.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/scene_pack_plan_2026-03-21.json)
- [generated/scene_pack_manifests_2026-03-21](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/scene_pack_manifests_2026-03-21)
- [generated/dirty_region_templates_2026-03-21](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/dirty_region_templates_2026-03-21)
- [generated/compiled_packs_2026-03-21](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/compiled_packs_2026-03-21)
- [generated/scene_transition_prefetch_report_2026-03-21.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/scene_transition_prefetch_report_2026-03-21.json)

## Current Runtime Architecture

The checked-in PS1 path is no longer just a replay-heavy software fallback.
The real current architecture is:

- family-scoped `.PAK` payloads staged under [jc_resources/packs](/home/hunter/workspace/jc_reborn/jc_resources/packs)
- self-describing pack TOCs with pack-authoritative resource lookup once a
  family pack is active
- offline-transcoded `PSB` sprite bundles for hot BMPs, with runtime preference
  for PSB over BMP where proven
- a dirty-row tile renderer in [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c)
  that restores/uploads only touched tile rows instead of whole-frame tiles
- O(1) resource lookup in [resource.c](/home/hunter/workspace/jc_reborn/resource.c)
  and cached restore-pilot lookups in [ads.c](/home/hunter/workspace/jc_reborn/ads.c)
  and [ttm.c](/home/hunter/workspace/jc_reborn/ttm.c)
- scene-scoped restore pilots in [ps1_restore_pilots.h](/home/hunter/workspace/jc_reborn/ps1_restore_pilots.h)
  rather than one-off hand-edited scene logic

The old replay/recovery machinery still exists, but it is now the shrinking
legacy boundary, not the whole architecture.

## What Each File Is For

- [CURRENT_STATUS_2026-03-21.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/CURRENT_STATUS_2026-03-21.md)
  Active rollout counts and what is actually verified right now.
- [IMPLEMENTATION_PLAN.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/IMPLEMENTATION_PLAN.md)
  The execution plan and architectural direction.
- [BACKLOG.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/BACKLOG.md)
  Parallelizable work tracks and queue ideas.
- [SDL_COMPAT_LITE_SPEC.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/SDL_COMPAT_LITE_SPEC.md)
  The runtime contract target.
- [IMPLEMENTATION_PLAN.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/IMPLEMENTATION_PLAN.md#conversion-and-verification-program)
  The step-by-step conversion and verification program.
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
- [scene_analysis_output_2026-03-21.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/scene_analysis_output_2026-03-21.json)
- [scene_pack_plan_2026-03-21.json](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/scene_pack_plan_2026-03-21.json)

That does not mean runtime RAM pressure is solved. It means the current bug surface
is more about semantic mismatch than raw impossibility:

- PC assumes persistent SDL layers with straightforward blit semantics.
- PS1 now uses pack-authoritative scene assets, dirty-row background restore,
  PSB/BMP sprite loading, and a smaller remaining replay/recovery boundary.
- The remaining regressions are concentrated where legacy replay continuity,
  slot carry, or sprite-path divergence still leak into correctness.

The most promising direction is:

1. Define a strict `SDL-Compat Lite` runtime contract for PS1.
2. Keep moving complexity into offline asset compilation and static scheduling.
3. Spend ISO space on pretranscoded, prevalidated, scene-oriented data.
4. Continue shrinking the remaining replay-era correctness boundary until it is
   no longer required for validated families.

The active execution sequence for finishing the port now lives in the
`Conversion And Verification Program` section of
[IMPLEMENTATION_PLAN.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/IMPLEMENTATION_PLAN.md#conversion-and-verification-program).
That section is explicitly aimed at eliminating the six-month
"Johnny disappears" bug class by:

- completing the offline artifact surface first so missing-data/manifests/specs
  stop being a default explanation
- then closing current bring-up/tail routes on top of that finished artifact
  boundary
- promoting by shared contract where validation is trustworthy
- fixing blocked entry paths
- removing replay continuity as a correctness dependency family by family


## Rollout History

The dated rollout chronology and implementation diary have been moved to:

- [ROLLOUT_HISTORY.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/ROLLOUT_HISTORY.md)

That file is historical context only. Use the current status snapshot and plan as
active references.
