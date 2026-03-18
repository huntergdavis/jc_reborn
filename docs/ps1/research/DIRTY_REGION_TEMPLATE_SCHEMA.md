# Dirty-Region Template Schema

Date: 2026-03-18
Status: Phase 7 draft

## Purpose

Describe the offline template artifact produced from scene-pack manifests plus
extracted `TTM` bytecode.

This is a planning/build artifact. It does not change runtime behavior by
itself.

## Generator

- [extract-dirty-region-templates.py](/home/hunter/workspace/jc_reborn/scripts/extract-dirty-region-templates.py)

Default output directory:

- [dirty_region_templates_2026-03-18](/home/hunter/workspace/jc_reborn/docs/ps1/research/dirty_region_templates_2026-03-18)

## Per-pack artifact

Each pack JSON contains:

- `schema_version`
- `artifact_kind = dirty_region_template_pack`
- `pack_id`
- `ads_names`
- `scene_indices`
- `summary`
- `ttm_templates`
- `scene_templates`

## `summary`

- `ttm_count`
- `restore_candidate_ttm_count`
- `scene_template_count`
- `candidate_scene_indices`

## `ttm_templates`

Each `TTM` row includes:

- `ttm_name`
- `byte_size`
- `op_counts`
- `unique_rect_count`
- `unique_rects`
- `union_rect`
- `clear_heavy`
- `restore_candidate`
- `tag_templates`
- `source_scene_indices`
- `slot_ids`

The extractor currently tracks these opcodes as dirty-region signals:

- `TTM_UNKNOWN_1`
- `COPY_ZONE_TO_BG`
- `SAVE_IMAGE1`
- `SAVE_ZONE`
- `RESTORE_ZONE`
- `CLEAR_SCREEN`

## `scene_templates`

Each scene row is a first-pass union over all referenced `TTM` templates:

- `scene_index`
- `ttm_names`
- `unique_rects`
- `unique_rect_count`
- `union_rect`
- `clear_screen_count`
- `restore_candidate`

## Aggregate summary

`summary.json` contains:

- `schema_version`
- `artifact_kind = dirty_region_template_summary`
- `pack_count`
- `candidate_pack_ids`
- `candidate_scene_total`
- `packs`

## Current limitations

- Templates are derived from static `TTM` bytecode only.
- No runtime validation is implied yet.
- Candidate selection is heuristic; dynamic overlap scenes may still require
  replay continuity.
