# PS1 Pack Manifest Schema

Date: 2026-03-17
Status: Draft

## Purpose

Define the minimum manifest contract for Phase 5 pack compilation work so that:

- analyzer-derived planning tools emit a stable intermediate format
- pack compiler work can start before the final binary pack layout is chosen
- transition and prefetch studies target the same field names

This is intentionally a JSON manifest schema draft, not a binary on-disc format.

## Design constraints

- One manifest represents one logical pack.
- Pack granularity is still open:
  - per ADS tag
  - per scene cluster
  - hybrid pack plus shared bank
- The schema must be producible from analyzer output plus post-processing tools.
- The schema must be consumable without scraping text reports.

## Required top-level fields

- `schema_version`
- `manifest_kind`
  - `scene_pack_manifest`
- `pack_id`
- `pack_strategy`
  - `ads_cluster`
  - `scene_tag`
  - `hybrid`
- `scene_indices`
- `ads_names`
- `resources`
- `runtime_requirements`
- `transition_hints`
- `prefetch_hints`

## `resources`

This section defines what the pack needs available for deterministic runtime use.

- `bmps`
  - array of BMP names
- `scrs`
  - array of SCR names
- `ttms`
  - array of TTM names
- `ads`
  - array of ADS names
- `shared_candidates`
  - resources that are likely worth moving into shared banks later

## `runtime_requirements`

This section captures the runtime envelope the pack must satisfy.

- `max_concurrent_threads`
- `max_sprite_frames`
- `peak_memory_bytes`
- `memory_components`
  - `bmp_indexed_bytes`
  - `ttm_bytes`
  - `ads_bytes`
  - `scr_bytes`
  - `sprite_pointer_bytes`
  - `ttm_slot_overhead_bytes`

If the pack covers multiple scenes, these values should represent the envelope for
the whole pack, not a single scene.

## `transition_hints`

This section is for pack-boundary and load-time planning.

- `likely_next_pack_ids`
- `high_churn_transitions`
  - references to scene-to-scene transitions with high resource churn
- `transition_budget_notes`
  - optional freeform note for current heuristics or known weak assumptions

## `prefetch_hints`

This section is for early streaming policy work.

- `candidate_scene_indices`
- `additional_bmps`
- `additional_scrs`
- `additional_ttms`
- `heuristic`
  - example: `ads_family_union`
- `confidence`
  - `low`
  - `medium`
  - `high`

Current expectation:

- all initial prefetch hints should be treated as heuristic
- confidence should stay `low` until we have validated transition data

## Example shape

```json
{
  "schema_version": 1,
  "manifest_kind": "scene_pack_manifest",
  "pack_id": "ads-cluster-activity",
  "pack_strategy": "ads_cluster",
  "scene_indices": [0, 1, 2],
  "ads_names": ["ACTIVITY.ADS"],
  "resources": {
    "bmps": ["GJDIVE.BMP", "MJDIVE.BMP", "JOHNWALK.BMP"],
    "scrs": ["ISLETEMP.SCR"],
    "ttms": ["GJDIVE.TTM", "MJDIVE.TTM"],
    "ads": ["ACTIVITY.ADS"],
    "shared_candidates": ["JOHNWALK.BMP"]
  },
  "runtime_requirements": {
    "max_concurrent_threads": 20,
    "max_sprite_frames": 160,
    "peak_memory_bytes": 436592,
    "memory_components": {
      "bmp_indexed_bytes": 228598,
      "ttm_bytes": 7288,
      "ads_bytes": 2528,
      "scr_bytes": 112038,
      "sprite_pointer_bytes": 640,
      "ttm_slot_overhead_bytes": 49152
    }
  },
  "transition_hints": {
    "likely_next_pack_ids": ["ads-cluster-building"],
    "high_churn_transitions": [
      { "from_scene_index": 9, "to_scene_index": 10, "total_resource_churn": 12 }
    ],
    "transition_budget_notes": "Derived from analyzer post-processing; not yet runtime-validated."
  },
  "prefetch_hints": {
    "candidate_scene_indices": [3, 4, 5],
    "additional_bmps": ["FISHPOLE.BMP"],
    "additional_scrs": [],
    "additional_ttms": ["GJNAT3.TTM"],
    "heuristic": "ads_family_union",
    "confidence": "low"
  }
}
```

## Near-term use

- pack-planning tools should emit this shape or a close superset
- transition/prefetch tools should emit fields that can be copied directly into it
- runtime loader work can key directly off `ads_names[0]` once the manifest set is
  stable enough to batch-compile
