# Transition / Prefetch Schema Notes

This file documents the post-processed planning artifacts generated from
`scene_analysis_output_2026-03-17.json`.

It is meant to feed the pack manifest draft in
[PACK_MANIFEST_SCHEMA.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/PACK_MANIFEST_SCHEMA.md).

## Inputs

- `scenes[*]` from the analyzer JSON
- per-scene resource bindings
- per-scene peak memory estimates

## Outputs

- `pack_candidates`
  - ADS-family groups with union resource accounting
  - useful for pack-granularity decisions
- `pack_manifest_inputs`
  - direct fields that map into `PACK_MANIFEST_SCHEMA.md`
- `transition_edges`
  - adjacent story-order scene edges
  - includes added/shared/removed resource counts and bytes
- `top_prefetch_edges`
  - ranked by added bytes and working set
- `top_pack_boundaries`
  - ranked by large working-set crossings or cross-family transitions

## Field Definitions

- `added_bytes`
  - bytes that appear in the destination scene but not the source scene
- `shared_bytes`
  - bytes that exist in both scenes
- `working_set_bytes`
  - union of source and destination resource bytes
- `edge_class`
  - heuristic label derived from `added_bytes` and whether the ADS family changed
- `prefetch_hint.priority`
  - `low`, `medium`, or `high`
  - reflects relative post-processing priority, not a runtime guarantee

## Caveats

- This is a post-processing planner, not a runtime transition validator.
- Scene ordering is the analyzer's story order, not an instrumented runtime trace.
- The `edge_class` labels are intentionally simple and should be replaced if the
  project gains validated transition telemetry.
