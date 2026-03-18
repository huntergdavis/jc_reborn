#!/usr/bin/env python3
"""Build pack-planning manifests from the analyzer JSON.

The planner groups scenes by the analyzer's heuristic ADS-family clusters and
emits:

- one index JSON file for the whole plan
- one manifest JSON file per pack

This is intentionally a consumer, not a new source of truth. It turns the
analyzer output into pack-shaped build input so later stages can draft a real
compiler and loader against a stable file layout.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterable, List, Mapping, MutableMapping, Sequence


@dataclass
class ResourceAggregate:
    name: str
    pack_scene_ref_count: int = 0
    global_scene_ref_count: int = 1
    bytes: int = 0
    indexed_bytes: int = 0
    raw_bytes: int = 0
    source_scene_indices: List[int] = field(default_factory=list)
    metadata: Dict[str, object] = field(default_factory=dict)


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def default_source_path() -> Path:
    return repo_root() / "docs" / "ps1" / "research" / "scene_analysis_output_2026-03-17.json"


def default_index_path() -> Path:
    return repo_root() / "docs" / "ps1" / "research" / "scene_pack_plan_2026-03-17.json"


def default_manifest_dir() -> Path:
    return repo_root() / "docs" / "ps1" / "research" / "scene_pack_manifests_2026-03-17"


def slugify(name: str) -> str:
    slug = name.lower()
    slug = slug.replace(".ads", "-ads")
    slug = re.sub(r"[^a-z0-9]+", "-", slug).strip("-")
    return slug or "pack"


def load_json(path: Path) -> Mapping[str, object]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def scene_lookup(analyzer: Mapping[str, object]) -> Dict[int, Mapping[str, object]]:
    scenes = analyzer.get("scenes", [])
    lookup: Dict[int, Mapping[str, object]] = {}
    for scene in scenes:
        lookup[int(scene["scene_index"])] = scene
    return lookup


def global_shared_lookup(analyzer: Mapping[str, object]) -> Dict[str, Dict[str, Mapping[str, object]]]:
    derived = analyzer.get("derived", {})
    shared = derived.get("shared_resources", {}) if isinstance(derived, Mapping) else {}
    result: Dict[str, Dict[str, Mapping[str, object]]] = {}
    for kind in ("bmps", "ttms"):
        items = shared.get(kind, []) if isinstance(shared, Mapping) else []
        result[kind] = {item["name"].lower(): item for item in items}
    return result


def shared_scene_ref_counts(analyzer: Mapping[str, object]) -> Dict[str, Dict[str, int]]:
    counts: Dict[str, Dict[str, int]] = {"bmps": {}, "scrs": {}, "ttms": {}}
    for scene in analyzer.get("scenes", []):
        for kind in ("bmps", "scrs", "ttms"):
            for item in scene.get("resources", {}).get(kind, []):
                key = item["name"].lower()
                counts[kind][key] = counts[kind].get(key, 0) + 1
    return counts


def group_clusters(analyzer: Mapping[str, object]) -> List[Mapping[str, object]]:
    derived = analyzer.get("derived", {})
    clusters = derived.get("candidate_scene_clusters", []) if isinstance(derived, Mapping) else []
    if clusters:
        return list(clusters)

    grouped: Dict[str, List[int]] = defaultdict(list)
    for scene in analyzer.get("scenes", []):
        grouped[str(scene["ads_name"])].append(int(scene["scene_index"]))
    return [
        {
            "cluster_key": key,
            "scene_indices": indices,
            "scene_count": len(indices),
            "max_peak_memory_bytes": max(
                int(analyzer["scenes"][i]["memory"]["peak_memory_bytes"]) for i in indices
            ),
            "total_peak_memory_bytes": sum(
                int(analyzer["scenes"][i]["memory"]["peak_memory_bytes"]) for i in indices
            ),
        }
        for key, indices in sorted(grouped.items())
    ]


def aggregate_resource(
    resources: Sequence[Mapping[str, object]],
    global_counts: Mapping[str, int],
    kind: str,
) -> List[ResourceAggregate]:
    aggregated: Dict[str, ResourceAggregate] = {}
    for scene_index, item in resources:
        name = str(item["name"])
        key = name.lower()
        entry = aggregated.get(key)
        if entry is None:
            entry = ResourceAggregate(name=name)
            aggregated[key] = entry
        entry.pack_scene_ref_count += 1
        entry.source_scene_indices.append(scene_index)
        if kind == "bmps":
            entry.raw_bytes = int(item.get("raw_bytes", 0))
            entry.indexed_bytes = int(item.get("indexed_bytes", 0))
            entry.bytes = entry.raw_bytes
            entry.metadata.setdefault("num_frames", int(item.get("num_frames", 0)))
            entry.metadata.setdefault("max_width", int(item.get("max_width", 0)))
            entry.metadata.setdefault("max_height", int(item.get("max_height", 0)))
            bindings = entry.metadata.setdefault("bindings", [])
            bindings.append(
                {
                    "scene_index": scene_index,
                    "ttm_slot": item.get("ttm_slot"),
                    "bmp_slot": item.get("bmp_slot"),
                }
            )
        elif kind == "ttms":
            entry.bytes = int(item.get("bytes", 0))
            slot_ids = entry.metadata.setdefault("slot_ids", [])
            slot_ids.append(item.get("slot_id"))
        elif kind == "scrs":
            entry.bytes = int(item.get("bytes", 0))
            entry.metadata.setdefault("bytes", int(item.get("bytes", 0)))

    for entry in aggregated.values():
        entry.global_scene_ref_count = global_counts.get(entry.name.lower(), 1)
        entry.source_scene_indices = sorted(set(entry.source_scene_indices))

    return sorted(aggregated.values(), key=lambda x: (-x.pack_scene_ref_count, x.name.lower()))


def scene_resource_names(scene: Mapping[str, object], kind: str) -> List[Mapping[str, object]]:
    return list(scene.get("resources", {}).get(kind, []))


def pack_prefetch_ids(
    cluster_scene_indices: Sequence[int],
    analyzer: Mapping[str, object],
    scene_to_pack_id: Mapping[int, str],
) -> List[str]:
    derived = analyzer.get("derived", {})
    prefetch_sets = derived.get("likely_prefetch_sets", []) if isinstance(derived, Mapping) else []
    cluster_set = set(cluster_scene_indices)
    weights: Counter[str] = Counter()

    for row in prefetch_sets:
        scene_index = int(row["scene_index"])
        if scene_index not in cluster_set:
            continue
        for candidate in row.get("candidate_scene_indices", []):
            candidate = int(candidate)
            pack_id = scene_to_pack_id.get(candidate)
            if pack_id:
                weights[pack_id] += 1

    cluster_pack_id = scene_to_pack_id.get(cluster_scene_indices[0]) if cluster_scene_indices else None
    if cluster_pack_id:
        weights.pop(cluster_pack_id, None)

    ordered = sorted(weights.items(), key=lambda kv: (-kv[1], kv[0]))
    return [pack_id for pack_id, _ in ordered]


def transition_prefetch_ids(
    cluster_scene_indices: Sequence[int],
    analyzer: Mapping[str, object],
    scene_to_pack_id: Mapping[int, str],
) -> List[str]:
    derived = analyzer.get("derived", {})
    transitions = derived.get("heaviest_transition_deltas", []) if isinstance(derived, Mapping) else []
    cluster_set = set(cluster_scene_indices)
    weights: Counter[str] = Counter()

    for row in transitions:
        from_scene = int(row["from_scene_index"])
        if from_scene not in cluster_set:
            continue
        to_scene = int(row["to_scene_index"])
        pack_id = scene_to_pack_id.get(to_scene)
        if pack_id:
            weights[pack_id] += max(1, int(row.get("total_resource_churn", 1)))

    cluster_pack_id = scene_to_pack_id.get(cluster_scene_indices[0]) if cluster_scene_indices else None
    if cluster_pack_id:
        weights.pop(cluster_pack_id, None)

    ordered = sorted(weights.items(), key=lambda kv: (-kv[1], kv[0]))
    return [pack_id for pack_id, _ in ordered]


def build_pack_manifest(
    cluster: Mapping[str, object],
    analyzer: Mapping[str, object],
    scenes: Mapping[int, Mapping[str, object]],
    global_counts: Mapping[str, Dict[str, int]],
    scene_to_pack_id: Mapping[int, str],
    pack_to_scene_indices: Mapping[str, Sequence[int]],
) -> Dict[str, object]:
    scene_indices = [int(i) for i in cluster.get("scene_indices", [])]
    cluster_key = str(cluster["cluster_key"])
    pack_id = slugify(cluster_key)
    bmp_items = []
    scr_items = []
    ttm_items = []
    for scene_index in scene_indices:
        scene = scenes[scene_index]
        bmp_items.extend((scene_index, item) for item in scene_resource_names(scene, "bmps"))
        scr_items.extend((scene_index, item) for item in scene_resource_names(scene, "scrs"))
        ttm_items.extend((scene_index, item) for item in scene_resource_names(scene, "ttms"))

    bmp_resources = aggregate_resource(bmp_items, global_counts["bmps"], "bmps")
    scr_resources = aggregate_resource(scr_items, global_counts["scrs"], "scrs")
    ttm_resources = aggregate_resource(ttm_items, global_counts["ttms"], "ttms")

    shared_bmps = [r for r in bmp_resources if r.global_scene_ref_count >= 2]
    shared_ttms = [r for r in ttm_resources if r.global_scene_ref_count >= 2]
    shared_scrs = [r for r in scr_resources if r.global_scene_ref_count >= 2]

    shared_bank = {
        "bmps": [
            {
                "name": r.name,
                "global_scene_ref_count": r.global_scene_ref_count,
                "pack_scene_ref_count": r.pack_scene_ref_count,
                "bytes": r.bytes,
                "raw_bytes": r.raw_bytes,
                "indexed_bytes": r.indexed_bytes,
            }
            for r in shared_bmps
        ],
        "scrs": [
            {
                "name": r.name,
                "global_scene_ref_count": r.global_scene_ref_count,
                "pack_scene_ref_count": r.pack_scene_ref_count,
                "bytes": r.bytes,
                "raw_bytes": r.raw_bytes,
                "indexed_bytes": r.indexed_bytes,
            }
            for r in shared_scrs
        ],
        "ttms": [
            {
                "name": r.name,
                "global_scene_ref_count": r.global_scene_ref_count,
                "pack_scene_ref_count": r.pack_scene_ref_count,
                "bytes": r.bytes,
                "raw_bytes": r.raw_bytes,
                "indexed_bytes": r.indexed_bytes,
            }
            for r in shared_ttms
        ],
    }

    pack_resources = {
        "bmps": [
            {
                "name": r.name,
                "pack_scene_ref_count": r.pack_scene_ref_count,
                "global_scene_ref_count": r.global_scene_ref_count,
                "bytes": r.bytes,
                "raw_bytes": r.raw_bytes,
                "indexed_bytes": r.indexed_bytes,
                "source_scene_indices": r.source_scene_indices,
                "metadata": r.metadata,
            }
            for r in bmp_resources
        ],
        "scrs": [
            {
                "name": r.name,
                "pack_scene_ref_count": r.pack_scene_ref_count,
                "global_scene_ref_count": r.global_scene_ref_count,
                "bytes": r.bytes,
                "raw_bytes": r.raw_bytes,
                "indexed_bytes": r.indexed_bytes,
                "source_scene_indices": r.source_scene_indices,
                "metadata": r.metadata,
            }
            for r in scr_resources
        ],
        "ttms": [
            {
                "name": r.name,
                "pack_scene_ref_count": r.pack_scene_ref_count,
                "global_scene_ref_count": r.global_scene_ref_count,
                "bytes": r.bytes,
                "raw_bytes": r.raw_bytes,
                "indexed_bytes": r.indexed_bytes,
                "source_scene_indices": r.source_scene_indices,
                "metadata": r.metadata,
            }
            for r in ttm_resources
        ],
    }

    transition_prefetch = transition_prefetch_ids(scene_indices, analyzer, scene_to_pack_id)
    family_prefetch = pack_prefetch_ids(scene_indices, analyzer, scene_to_pack_id)
    prefetch_pack_ids = transition_prefetch or family_prefetch
    prefetch_reason = "transition_churn_then_ads_family_union" if transition_prefetch else "ads_family_union"

    pack_scene_set = set(scene_indices)
    candidate_scene_indices: List[int] = []
    seen_candidate_scenes = set()
    for candidate_pack_id in prefetch_pack_ids:
        for candidate_scene_index in pack_to_scene_indices.get(candidate_pack_id, []):
            candidate_scene_index = int(candidate_scene_index)
            if candidate_scene_index in pack_scene_set or candidate_scene_index in seen_candidate_scenes:
                continue
            seen_candidate_scenes.add(candidate_scene_index)
            candidate_scene_indices.append(candidate_scene_index)

    candidate_resource_names = {"bmps": set(), "scrs": set(), "ttms": set()}
    for candidate_scene_index in candidate_scene_indices:
        candidate_scene = scenes[candidate_scene_index]
        for kind in ("bmps", "scrs", "ttms"):
            for item in scene_resource_names(candidate_scene, kind):
                if kind == "bmps":
                    pack_names = {r.name.lower() for r in bmp_resources}
                elif kind == "scrs":
                    pack_names = {r.name.lower() for r in scr_resources}
                else:
                    pack_names = {r.name.lower() for r in ttm_resources}
                if item["name"].lower() not in pack_names:
                    candidate_resource_names[kind].add(item["name"])

    runtime_memory_components = {
        "bmp_indexed_bytes": max((int(scenes[i]["memory"]["bmp_indexed_bytes"]) for i in scene_indices), default=0),
        "ttm_bytes": max((int(scenes[i]["memory"]["ttm_bytes"]) for i in scene_indices), default=0),
        "ads_bytes": max((int(scenes[i]["memory"]["ads_bytes"]) for i in scene_indices), default=0),
        "scr_bytes": max((int(scenes[i]["memory"]["scr_bytes"]) for i in scene_indices), default=0),
        "sprite_pointer_bytes": max((int(scenes[i]["memory"]["sprite_pointer_bytes"]) for i in scene_indices), default=0),
        "ttm_slot_overhead_bytes": max((int(scenes[i]["memory"]["ttm_slot_overhead_bytes"]) for i in scene_indices), default=0),
    }
    runtime_requirements = {
        "max_concurrent_threads": max((int(scenes[i]["max_concurrent_threads"]) for i in scene_indices), default=0),
        "max_sprite_frames": max((int(scenes[i]["total_sprite_frames"]) for i in scene_indices), default=0),
        "peak_memory_bytes": int(cluster["max_peak_memory_bytes"]),
        "memory_components": runtime_memory_components,
    }

    pack_transition_hints = []
    for row in analyzer.get("derived", {}).get("heaviest_transition_deltas", []):
        from_scene = int(row["from_scene_index"])
        to_scene = int(row["to_scene_index"])
        if from_scene not in pack_scene_set:
            continue
        pack_transition_hints.append(
            {
                "from_scene_index": from_scene,
                "to_scene_index": to_scene,
                "to_pack_id": scene_to_pack_id.get(to_scene),
                "total_resource_churn": int(row.get("total_resource_churn", 0)),
                "peak_memory_delta_bytes": int(row.get("peak_memory_delta_bytes", 0)),
            }
        )

    pack_transition_hints.sort(key=lambda item: (-item["total_resource_churn"], -item["peak_memory_delta_bytes"]))
    pack_transition_hints = pack_transition_hints[:5]

    scenes_payload = []
    for scene_index in scene_indices:
        scene = scenes[scene_index]
        scenes_payload.append(
            {
                "scene_index": scene_index,
                "ads_name": scene["ads_name"],
                "ads_tag": scene["ads_tag"],
                "story_day": scene["story_day"],
                "flags": scene["flags"],
                "memory": scene["memory"],
                "resource_counts": {
                    "bmps": len(scene_resource_names(scene, "bmps")),
                    "scrs": len(scene_resource_names(scene, "scrs")),
                    "ttms": len(scene_resource_names(scene, "ttms")),
                },
            }
        )

    return {
        "schema_version": 1,
        "manifest_kind": "scene_pack_manifest",
        "pack_id": pack_id,
        "cluster_key": cluster_key,
        "pack_strategy": "ads_cluster",
        "scene_indices": scene_indices,
        "scene_count": len(scene_indices),
        "ads_names": [cluster_key],
        "resources": {
            "ads": [cluster_key],
            "bmps": [r.name for r in bmp_resources],
            "scrs": [r.name for r in scr_resources],
            "ttms": [r.name for r in ttm_resources],
            "shared_candidates": sorted(
                {r.name for r in shared_bmps + shared_scrs + shared_ttms}
            ),
        },
        "runtime_requirements": runtime_requirements,
        "transition_hints": {
            "likely_next_pack_ids": prefetch_pack_ids,
            "high_churn_transitions": pack_transition_hints,
            "transition_budget_notes": "Derived from analyzer post-processing; not runtime-validated.",
        },
        "prefetch_hints": {
            "candidate_scene_indices": candidate_scene_indices,
            "additional_bmps": sorted(candidate_resource_names["bmps"]),
            "additional_scrs": sorted(candidate_resource_names["scrs"]),
            "additional_ttms": sorted(candidate_resource_names["ttms"]),
            "heuristic": prefetch_reason,
            "confidence": "low",
        },
        "scenes": scenes_payload,
        "shared_bank": shared_bank,
        "resource_details": pack_resources,
        "prefetch": {
            "heuristic": prefetch_reason,
            "candidate_pack_ids": prefetch_pack_ids,
            "secondary_candidate_pack_ids": family_prefetch if transition_prefetch else [],
        },
        "max_peak_memory_bytes": int(cluster["max_peak_memory_bytes"]),
        "total_peak_memory_bytes": int(cluster["total_peak_memory_bytes"]),
    }


def write_json(path: Path, payload: Mapping[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2)
        f.write("\n")


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate pack-planning artifacts from analyzer JSON.")
    ap.add_argument(
        "source",
        nargs="?",
        type=Path,
        default=default_source_path(),
        help="Analyzer JSON input (default: docs/ps1/research/scene_analysis_output_2026-03-17.json)",
    )
    ap.add_argument(
        "--output",
        type=Path,
        default=default_index_path(),
        help="Write the aggregate plan JSON here (default: docs/ps1/research/scene_pack_plan_2026-03-17.json)",
    )
    ap.add_argument(
        "--manifest-dir",
        type=Path,
        default=default_manifest_dir(),
        help="Write per-pack manifest JSON files into this directory",
    )
    ap.add_argument("--stdout", action="store_true", help="Also print the aggregate plan to stdout")
    args = ap.parse_args()

    analyzer = load_json(args.source)
    scenes = scene_lookup(analyzer)
    clusters = group_clusters(analyzer)
    global_counts = shared_scene_ref_counts(analyzer)
    global_shared = global_shared_lookup(analyzer)
    pack_to_scene_indices = {
        slugify(str(cluster_item["cluster_key"])): [int(i) for i in cluster_item.get("scene_indices", [])]
        for cluster_item in clusters
    }

    scene_to_pack_id: Dict[int, str] = {}
    for cluster in clusters:
        pack_id = slugify(str(cluster["cluster_key"]))
        for scene_index in cluster.get("scene_indices", []):
            scene_to_pack_id[int(scene_index)] = pack_id

    packs = [
        build_pack_manifest(cluster, analyzer, scenes, global_counts, scene_to_pack_id, pack_to_scene_indices)
        for cluster in clusters
    ]

    index = {
        "planner_version": 1,
        "schema_version": 1,
        "manifest_kind": "scene_pack_plan",
        "source": {
            "analysis_file": str(args.source),
            "analysis_schema_version": analyzer.get("schema_version"),
        },
        "strategy": "ads-family-cluster",
        "pack_count": len(packs),
        "packs": [
            {
                "pack_id": pack["pack_id"],
                "cluster_key": pack["cluster_key"],
                "scene_count": pack["scene_count"],
                "max_peak_memory_bytes": pack["max_peak_memory_bytes"],
                "total_peak_memory_bytes": pack["total_peak_memory_bytes"],
                "manifest_file": str((args.manifest_dir / f"{pack['pack_id']}.json").as_posix()),
                "prefetch_candidate_pack_ids": pack["prefetch"]["candidate_pack_ids"],
            }
            for pack in packs
        ],
        "scene_to_pack_id": {str(k): v for k, v in sorted(scene_to_pack_id.items())},
        "shared_global_resources": {
            "bmps": list(global_shared["bmps"].values()),
            "ttms": list(global_shared["ttms"].values()),
        },
    }

    args.manifest_dir.mkdir(parents=True, exist_ok=True)
    for pack in packs:
        write_json(args.manifest_dir / f"{pack['pack_id']}.json", pack)

    write_json(args.output, index)
    if args.stdout:
        json.dump(index, sys.stdout, indent=2)
        sys.stdout.write("\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
