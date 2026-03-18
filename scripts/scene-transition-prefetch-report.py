#!/usr/bin/env python3
"""Post-process analyzer JSON into transition/prefetch planning output.

This tool does not re-analyze raw resources. It consumes the checked-in analyzer
JSON and derives more actionable planning data:

- per-cluster pack candidates
- story-order transition edges with resource churn
- pack-manifest-shaped inputs for downstream compilation work
- prefetch recommendations ranked by added working set

The output is intentionally conservative about confidence. It is a planning aid,
not a validated runtime transition graph.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterable, List, Tuple


DEFAULT_INPUT = Path("docs/ps1/research/scene_analysis_output_2026-03-17.json")
DEFAULT_JSON_OUTPUT = Path("docs/ps1/research/scene_transition_prefetch_report_2026-03-17.json")
DEFAULT_MD_OUTPUT = Path("docs/ps1/research/scene_transition_prefetch_report_2026-03-17.md")
DEFAULT_SCHEMA_NOTE = Path("docs/ps1/research/TRANSITION_PREFETCH_SCHEMA.md")
PS1_MEMORY_BUDGET_BYTES = 614400


@dataclass(frozen=True)
class ResourceEntry:
    kind: str
    name: str
    bytes: int

    @property
    def key(self) -> str:
        return f"{self.kind}:{self.name.upper()}"


@dataclass
class SceneEntry:
    index: int
    ads_name: str
    ads_tag: int
    max_concurrent_threads: int
    max_sprite_frames: int
    peak_memory_bytes: int
    memory_components: Dict[str, int]
    resources: List[ResourceEntry] = field(default_factory=list)

    @property
    def resource_keys(self) -> set[str]:
        return {res.key for res in self.resources}

    @property
    def resource_bytes_by_key(self) -> Dict[str, int]:
        return {res.key: res.bytes for res in self.resources}


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def build_scenes(obj: dict) -> List[SceneEntry]:
    scenes: List[SceneEntry] = []
    for scene in obj["scenes"]:
        resources: List[ResourceEntry] = []
        for item in scene["resources"]["bmps"]:
            resources.append(ResourceEntry("bmp", item["name"], int(item["indexed_bytes"])))
        for item in scene["resources"]["scrs"]:
            resources.append(ResourceEntry("scr", item["name"], int(item["bytes"])))
        for item in scene["resources"]["ttms"]:
            resources.append(ResourceEntry("ttm", item["name"], int(item["bytes"])))
        scenes.append(
            SceneEntry(
                index=int(scene["scene_index"]),
                ads_name=scene["ads_name"],
                ads_tag=int(scene["ads_tag"]),
                max_concurrent_threads=int(scene["max_concurrent_threads"]),
                max_sprite_frames=int(scene["total_sprite_frames"]),
                peak_memory_bytes=int(scene["memory"]["peak_memory_bytes"]),
                memory_components={k: int(v) for k, v in scene["memory"].items()},
                resources=resources,
            )
        )
    return scenes


def resource_union_bytes(scenes: Iterable[SceneEntry]) -> Tuple[int, Dict[str, int]]:
    union: Dict[str, int] = {}
    for scene in scenes:
        for res in scene.resources:
            union.setdefault(res.key, res.bytes)
    return sum(union.values()), union


def format_kb(value: int) -> str:
    return f"{value / 1024.0:.1f} KB"


def classify_transition(edge: dict) -> str:
    if edge["same_ads_family"]:
        if edge["added_bytes"] <= 64 * 1024:
            return "warm"
        if edge["added_bytes"] <= 128 * 1024:
            return "hot"
        return "heavy"
    if edge["added_bytes"] <= 96 * 1024:
        return "cold"
    return "heavy"


def compute_report(obj: dict) -> dict:
    scenes = build_scenes(obj)
    validation = {
        "schema_version": obj.get("schema_version"),
        "scene_count": len(scenes),
        "scene_index_sequence_ok": [scene.index for scene in scenes] == list(range(len(scenes))),
        "uses_ps1_pointer_size": obj.get("assumptions", {}).get("pointer_size_bytes") == 4,
    }

    cluster_map: Dict[str, List[SceneEntry]] = {}
    for scene in scenes:
        cluster_map.setdefault(scene.ads_name, []).append(scene)
    cluster_order = sorted(cluster_map.keys(), key=lambda name: cluster_map[name][0].index)

    pack_candidates = []
    for cluster_name in cluster_order:
        cluster_scenes = cluster_map[cluster_name]
        scene_indices = [scene.index for scene in cluster_scenes]
        total_peak = sum(scene.peak_memory_bytes for scene in cluster_scenes)
        max_scene = max(cluster_scenes, key=lambda s: s.peak_memory_bytes)
        union_bytes, union_map = resource_union_bytes(cluster_scenes)
        pack_candidates.append(
            {
                "cluster_key": cluster_name,
                "scene_count": len(cluster_scenes),
                "scene_indices": scene_indices,
                "scene_index_span": {"first": scene_indices[0], "last": scene_indices[-1]},
                "max_peak_memory_bytes": max_scene.peak_memory_bytes,
                "max_peak_scene_index": max_scene.index,
                "max_peak_scene_tag": max_scene.ads_tag,
                "total_peak_memory_bytes": total_peak,
                "union_resource_bytes": union_bytes,
                "union_resource_count": len(union_map),
                "fits_ps1_memory_budget": union_bytes <= PS1_MEMORY_BUDGET_BYTES,
                "budget_margin_bytes": PS1_MEMORY_BUDGET_BYTES - union_bytes,
            }
        )

    transitions = []
    for idx in range(len(scenes) - 1):
        from_scene = scenes[idx]
        to_scene = scenes[idx + 1]
        from_keys = from_scene.resource_keys
        to_keys = to_scene.resource_keys
        shared_keys = from_keys & to_keys
        added_keys = to_keys - from_keys
        removed_keys = from_keys - to_keys
        to_bytes = to_scene.resource_bytes_by_key
        from_bytes = from_scene.resource_bytes_by_key

        added_bytes = sum(to_bytes[k] for k in added_keys)
        removed_bytes = sum(from_bytes[k] for k in removed_keys)
        shared_bytes = sum(min(from_bytes[k], to_bytes[k]) for k in shared_keys)
        working_set_bytes = sum({**from_scene.resource_bytes_by_key, **to_scene.resource_bytes_by_key}.values())
        edge = {
            "from_scene_index": from_scene.index,
            "to_scene_index": to_scene.index,
            "from_ads_name": from_scene.ads_name,
            "to_ads_name": to_scene.ads_name,
            "same_ads_family": from_scene.ads_name == to_scene.ads_name,
            "added_resource_count": len(added_keys),
            "removed_resource_count": len(removed_keys),
            "shared_resource_count": len(shared_keys),
            "added_bytes": added_bytes,
            "removed_bytes": removed_bytes,
            "shared_bytes": shared_bytes,
            "working_set_bytes": working_set_bytes,
            "story_order_index": idx,
        }
        edge["edge_class"] = classify_transition(edge)
        edge["prefetch_hint"] = {
            "priority": "high" if edge["edge_class"] in ("warm", "hot") else "medium" if edge["same_ads_family"] else "low",
            "target_scene_index": to_scene.index,
            "target_ads_name": to_scene.ads_name,
            "prefetch_bytes": added_bytes,
            "rationale": (
                "same ADS family and low churn"
                if edge["edge_class"] == "warm"
                else "same ADS family with moderate churn"
                if edge["edge_class"] == "hot"
                else "cross-family handoff or large added working set"
            ),
        }
        transitions.append(edge)

    edge_lookup = {
        (edge["from_scene_index"], edge["to_scene_index"]): edge
        for edge in transitions
    }

    pack_manifest_inputs = []
    for idx, cluster_name in enumerate(cluster_order):
        cluster_scenes = cluster_map[cluster_name]
        max_scene = max(cluster_scenes, key=lambda s: s.peak_memory_bytes)
        union_bytes, _ = resource_union_bytes(cluster_scenes)

        resource_lists: Dict[str, List[str]] = {"bmp": [], "scr": [], "ttm": []}
        resource_counts: Dict[str, Dict[str, int]] = {"bmp": {}, "scr": {}, "ttm": {}}
        for scene in cluster_scenes:
            for res in scene.resources:
                resource_lists[res.kind].append(res.name)
                resource_counts[res.kind][res.name] = resource_counts[res.kind].get(res.name, 0) + 1

        deduped_resources = {kind: list(dict.fromkeys(names)) for kind, names in resource_lists.items()}
        shared_candidates = sorted(
            {
                name
                for kind_counts in resource_counts.values()
                for name, count in kind_counts.items()
                if count > 1
            }
        )

        next_cluster_name = cluster_order[idx + 1] if idx + 1 < len(cluster_order) else None
        if next_cluster_name:
            next_cluster_scenes = cluster_map[next_cluster_name]
            boundary_edge = edge_lookup[(cluster_scenes[-1].index, next_cluster_scenes[0].index)]
            next_scene = next_cluster_scenes[0]
            from_bmp_names = {res.name for res in cluster_scenes[-1].resources if res.kind == "bmp"}
            from_scr_names = {res.name for res in cluster_scenes[-1].resources if res.kind == "scr"}
            from_ttm_names = {res.name for res in cluster_scenes[-1].resources if res.kind == "ttm"}
            next_bmps = [res.name for res in next_scene.resources if res.kind == "bmp" and res.name not in from_bmp_names]
            next_scrs = [res.name for res in next_scene.resources if res.kind == "scr" and res.name not in from_scr_names]
            next_ttms = [res.name for res in next_scene.resources if res.kind == "ttm" and res.name not in from_ttm_names]
            likely_next_pack_ids = [f"ads-cluster-{next_cluster_name.lower().replace('.ads', '')}"]
            high_churn = [
                {
                    "from_scene_index": boundary_edge["from_scene_index"],
                    "to_scene_index": boundary_edge["to_scene_index"],
                    "total_resource_churn": boundary_edge["added_resource_count"] + boundary_edge["removed_resource_count"],
                    "added_bytes": boundary_edge["added_bytes"],
                    "working_set_bytes": boundary_edge["working_set_bytes"],
                }
            ]
            confidence = "low"
            candidate_scene_indices = [next_scene.index]
        else:
            next_bmps = []
            next_scrs = []
            next_ttms = []
            likely_next_pack_ids = []
            high_churn = []
            confidence = "none"
            candidate_scene_indices = []

        pack_manifest_inputs.append(
            {
                "schema_version": 1,
                "manifest_kind": "scene_pack_manifest",
                "pack_id": f"ads-cluster-{cluster_name.lower().replace('.ads', '')}",
                "pack_strategy": "ads_cluster",
                "scene_indices": [scene.index for scene in cluster_scenes],
                "ads_names": [cluster_name],
                "resources": {
                    "bmps": deduped_resources["bmp"],
                    "scrs": deduped_resources["scr"],
                    "ttms": deduped_resources["ttm"],
                    "ads": [cluster_name],
                    "shared_candidates": shared_candidates,
                },
                "runtime_requirements": {
                    "max_concurrent_threads": max_scene.max_concurrent_threads,
                    "max_sprite_frames": max_scene.max_sprite_frames,
                    "peak_memory_bytes": max_scene.peak_memory_bytes,
                    "memory_components": max_scene.memory_components,
                    "union_resource_bytes": union_bytes,
                },
                "transition_hints": {
                    "likely_next_pack_ids": likely_next_pack_ids,
                    "high_churn_transitions": high_churn,
                    "transition_budget_notes": "Derived from analyzer post-processing; not yet runtime-validated.",
                },
                "prefetch_hints": {
                    "candidate_scene_indices": candidate_scene_indices,
                    "additional_bmps": next_bmps,
                    "additional_scrs": next_scrs,
                    "additional_ttms": next_ttms,
                    "heuristic": "story_order_cluster_boundary",
                    "confidence": confidence,
                },
            }
        )

    transitions_sorted = sorted(
        transitions,
        key=lambda edge: (edge["added_bytes"], edge["working_set_bytes"], edge["story_order_index"]),
        reverse=True,
    )
    top_prefetch_edges = transitions_sorted[:12]
    top_pack_boundaries = [
        edge
        for edge in sorted(
            transitions,
            key=lambda edge: (edge["working_set_bytes"], edge["added_bytes"], edge["story_order_index"]),
            reverse=True,
        )
        if not edge["same_ads_family"] or edge["added_bytes"] > 96 * 1024
    ][:12]

    return {
        "schema_version": 1,
        "generated_from": "docs/ps1/research/scene_analysis_output_2026-03-17.json",
        "generated_note": "Derived post-processing output; not a runtime transition graph.",
        "validation": validation,
        "caveats": [
            "Scene ordering comes from the analyzer's story order, not from instrumented runtime control flow.",
            "Prefetch hints are based on resource churn between adjacent analyzer scenes.",
            "Pack candidates are ADS-family groups with unioned resource sets; they are not yet validated CD layouts.",
        ],
        "pack_candidates": pack_candidates,
        "pack_manifest_inputs": pack_manifest_inputs,
        "transition_edges": transitions,
        "top_prefetch_edges": top_prefetch_edges,
        "top_pack_boundaries": top_pack_boundaries,
    }


def write_json(path: Path, report: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


def write_markdown(path: Path, report: dict) -> None:
    lines: List[str] = []
    lines.append("# Scene Transition and Prefetch Report")
    lines.append("")
    lines.append("Generated from `scene_analysis_output_2026-03-17.json`.")
    lines.append("")
    lines.append("## Caveats")
    for caveat in report["caveats"]:
        lines.append(f"- {caveat}")
    lines.append("")
    lines.append("## Pack Candidates")
    for pack in sorted(report["pack_candidates"], key=lambda item: item["union_resource_bytes"], reverse=True):
        lines.append(
            f"- `{pack['cluster_key']}`: {pack['scene_count']} scenes, "
            f"union {format_kb(pack['union_resource_bytes'])}, "
            f"peak {format_kb(pack['max_peak_memory_bytes'])}, "
            f"{'fits' if pack['fits_ps1_memory_budget'] else 'exceeds'} PS1 budget"
        )
    lines.append("")
    lines.append("## Top Prefetch Edges")
    for edge in report["top_prefetch_edges"]:
        lines.append(
            f"- {edge['from_scene_index']} -> {edge['to_scene_index']} "
            f"(`{edge['from_ads_name']}` -> `{edge['to_ads_name']}`): "
            f"add {format_kb(edge['added_bytes'])}, shared {format_kb(edge['shared_bytes'])}, "
            f"class `{edge['edge_class']}`"
        )
    lines.append("")
    lines.append("## Top Pack Boundaries")
    for edge in report["top_pack_boundaries"]:
        lines.append(
            f"- {edge['from_scene_index']} -> {edge['to_scene_index']} "
            f"working set {format_kb(edge['working_set_bytes'])}, "
            f"added {format_kb(edge['added_bytes'])}, "
            f"class `{edge['edge_class']}`"
        )
    lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_schema_note(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        """# Transition / Prefetch Schema Notes

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
""",
        encoding="utf-8",
    )


def main() -> int:
    ap = argparse.ArgumentParser(description="Derive transition/prefetch planning data from analyzer JSON.")
    ap.add_argument("--input", type=Path, default=DEFAULT_INPUT, help="Analyzer JSON input path")
    ap.add_argument("--json-out", type=Path, default=DEFAULT_JSON_OUTPUT, help="Derived JSON output path")
    ap.add_argument("--md-out", type=Path, default=DEFAULT_MD_OUTPUT, help="Derived markdown output path")
    ap.add_argument("--schema-note", type=Path, default=DEFAULT_SCHEMA_NOTE, help="Schema note output path")
    ap.add_argument("--no-md", action="store_true", help="Skip markdown generation")
    ap.add_argument("--no-schema-note", action="store_true", help="Skip schema note generation")
    args = ap.parse_args()

    analyzer = load_json(args.input)
    report = compute_report(analyzer)

    write_json(args.json_out, report)
    if not args.no_md:
        write_markdown(args.md_out, report)
    if not args.no_schema_note:
        write_schema_note(args.schema_note)

    print(f"Wrote {args.json_out}")
    if not args.no_md:
        print(f"Wrote {args.md_out}")
    if not args.no_schema_note:
        print(f"Wrote {args.schema_note}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
