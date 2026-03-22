# Generated Research Artifacts

This directory holds bulk generated datasets and compiled research outputs.
These files are useful as build/debug inputs, but they are not the best entry
point for understanding the current rollout.

## Read first

- [../CURRENT_STATUS_2026-03-21.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/CURRENT_STATUS_2026-03-21.md)
- [../README.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/README.md)
- [../IMPLEMENTATION_PLAN.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/IMPLEMENTATION_PLAN.md)

## Main generated groups

- `scene_analysis_output_2026-03-17.*`
  Analyzer output snapshots.
- `scene_pack_plan_2026-03-17.json`
  First aggregate pack-planning output.
- `scene_pack_manifests_2026-03-17/`
  One manifest per ADS-family pack.
- `compiled_packs_2026-03-17/`
  Research pack payloads and indices.
- `scene_transition_prefetch_report_2026-03-17.*`
  Transition/prefetch planning output.
- `dirty_region_templates_2026-03-18/`
  Offline dirty-region candidates.
- `restore_candidate_report_full_2026-03-19.*`
  Ranked restore-candidate report across the full surface.
- `restore_pilot_specs_2026-03-19/`
  First smaller pilot-spec batch.
- `restore_pilot_specs_full_2026-03-19/`
  First ten-family pilot-spec batch.
- `restore_scene_specs_full_2026-03-19/`
  Full scene-scoped restore-spec batch.
- `restore_scene_clusters_2026-03-19.*`
  Cluster report that compresses scenes into shared contracts.
- `restore_cluster_specs_2026-03-19/`
  Reusable grouped restore contracts.

## Notes

- Paths in these generated files are expected to point back into this
  `generated/` subtree.
- Historical one-off pilot artifacts and stale rollout snapshots live under
  [../archive/README.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/archive/README.md),
  not here.
