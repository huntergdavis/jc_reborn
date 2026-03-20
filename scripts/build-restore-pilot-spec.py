#!/usr/bin/env python3
"""Build one or more scene-level restore pilot specs from ranked candidates."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Iterable


DEFAULT_REPORT = Path("docs/ps1/research/restore_candidate_report_full_2026-03-19.json")
DEFAULT_MANIFEST_DIR = Path("docs/ps1/research/scene_pack_manifests_2026-03-17")
DEFAULT_TEMPLATE_DIR = Path("docs/ps1/research/dirty_region_templates_2026-03-18")
DEFAULT_SCENE_ANALYSIS = Path("docs/ps1/research/scene_analysis_output_2026-03-17.json")
DEFAULT_JSON_OUTPUT = Path("docs/ps1/research/restore_pilot_spec_2026-03-18.json")
DEFAULT_MD_OUTPUT = Path("docs/ps1/research/restore_pilot_spec_2026-03-18.md")


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def build_spec(
    candidate: dict,
    manifest_dir: Path,
    template_dir: Path,
    scene_analysis: dict,
) -> dict:
    manifest = load_json(manifest_dir / f"{candidate['pack_id']}.json")
    template = load_json(template_dir / f"{candidate['pack_id']}.json")
    scene = next(row for row in manifest["scenes"] if row["scene_index"] == candidate["scene_index"])
    analyzed_scene = next(
        row
        for row in scene_analysis["scenes"]
        if row["ads_name"] == candidate["ads_name"] and row["scene_index"] == candidate["scene_index"]
    )
    scene_template = next(row for row in template["scene_templates"] if row["scene_index"] == candidate["scene_index"])
    compatible_scenes = []
    for scene_row in manifest["scenes"]:
        tmpl_row = next((row for row in template["scene_templates"] if row["scene_index"] == scene_row["scene_index"]), None)
        if tmpl_row is None:
            continue
        if scene_row["ads_name"] != candidate["ads_name"]:
            continue
        if scene_row["resource_counts"] != scene["resource_counts"]:
            continue
        if tmpl_row["ttm_names"] != scene_template["ttm_names"]:
            continue
        if tmpl_row["union_rect"] != scene_template["union_rect"]:
            continue
        compatible_scenes.append(
            {
                "scene_index": scene_row["scene_index"],
                "ads_tag": scene_row["ads_tag"],
            }
        )
    compatible_scenes.sort(key=lambda row: row["ads_tag"])
    start_idx = next(
        (i for i, row in enumerate(compatible_scenes) if row["ads_tag"] == candidate["ads_tag"]),
        0,
    )
    compatible_scenes = compatible_scenes[start_idx:start_idx + 3]
    ttm_templates = [
        row for row in template["ttm_templates"]
        if row["ttm_name"] in scene_template["ttm_names"]
    ]

    return {
        "schema_version": 1,
        "artifact_kind": "restore_pilot_spec",
        "selected_scene": candidate,
        "scene_flags": scene["flags"],
        "memory": scene["memory"],
        "resource_counts": scene["resource_counts"],
        "scene_resources": {
            "bmps": [row["name"] for row in analyzed_scene["resources"]["bmps"]],
            "scrs": [row["name"] for row in analyzed_scene["resources"]["scrs"]],
            "ttms": [row["name"] for row in analyzed_scene["resources"]["ttms"]],
        },
        "restore_template": {
            "union_rect": scene_template["union_rect"],
            "unique_rects": scene_template["unique_rects"],
            "clear_screen_count": scene_template["clear_screen_count"],
            "ttm_names": scene_template["ttm_names"],
        },
        "ttm_details": [
            {
                "ttm_name": row["ttm_name"],
                "union_rect": row["union_rect"],
                "unique_rects": row["unique_rects"],
                "clear_region_ids": row["unique_clear_region_ids"],
                "op_counts": row["op_counts"],
            }
            for row in sorted(ttm_templates, key=lambda item: item["ttm_name"])
        ],
        "recommended_runtime_scope": {
            "ads_name": candidate["ads_name"],
            "ads_tags": [row["ads_tag"] for row in compatible_scenes],
            "scene_indices": [row["scene_index"] for row in compatible_scenes],
            "ttm_names": scene_template["ttm_names"],
            "notes": [
                "Keep pack path authoritative; do not reintroduce extracted fallback.",
                "Scope runtime attempts to the compatible scene cluster only, not the whole ADS family.",
                "Prefer rect-restore validation around the listed TTM names before touching replay carry logic.",
            ],
        },
    }


def slugify_ads_name(ads_name: str) -> str:
    return ads_name.lower().replace(".", "-")


def select_candidates(
    report: dict,
    ads_name: str | None,
    limit: int | None,
    candidate_set: str,
) -> list[dict]:
    if candidate_set in report:
        rows = list(report[candidate_set])
    elif candidate_set == "all_candidates" and "top_candidates" in report:
        rows = list(report["top_candidates"])
    else:
        raise KeyError(candidate_set)
    if ads_name is not None:
        rows = [row for row in rows if row["ads_name"] == ads_name]
    if limit is not None:
        rows = rows[:limit]
    return rows


def build_batch_summary(specs: Iterable[dict], output_dir: Path, scene_scoped: bool) -> dict:
    rows = []
    for spec in specs:
        sel = spec["selected_scene"]
        slug = slugify_scene(sel["ads_name"], sel["ads_tag"]) if scene_scoped else slugify_ads_name(sel["ads_name"])
        rows.append(
            {
                "ads_name": sel["ads_name"],
                "ads_tag": sel["ads_tag"],
                "scene_index": sel["scene_index"],
                "pack_id": sel["pack_id"],
                "restore_score": sel["restore_score"],
                "ttm_names": spec["restore_template"]["ttm_names"],
                "union_rect": spec["restore_template"]["union_rect"],
                "json_path": str(output_dir / f"{slug}.json"),
                "md_path": str(output_dir / f"{slug}.md"),
            }
        )
    return {
        "schema_version": 1,
        "artifact_kind": "restore_pilot_spec_batch",
        "pilot_count": len(rows),
        "pilots": rows,
    }


def slugify_scene(ads_name: str, ads_tag: int) -> str:
    return f"{slugify_ads_name(ads_name)}-tag-{ads_tag:02d}"


def write_batch_markdown(path: Path, summary: dict) -> None:
    lines = [
        "# Restore Pilot Spec Batch",
        "",
        "Date: 2026-03-19",
        "",
        f"Pilot count: `{summary['pilot_count']}`",
        "",
        "## Generated pilots",
        "",
    ]
    for row in summary["pilots"]:
        lines.append(
            f"- `{row['ads_name']} tag {row['ads_tag']}`"
            f" from `{row['pack_id']}`: scene `{row['scene_index']}`,"
            f" score `{row['restore_score']}`, ttms `{', '.join(row['ttm_names'])}`"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_markdown(path: Path, spec: dict) -> None:
    sel = spec["selected_scene"]
    lines = [
        "# Restore Pilot Spec",
        "",
        "Date: 2026-03-18",
        "",
        f"Selected pilot: `{sel['ads_name']} tag {sel['ads_tag']}` from `{sel['pack_id']}`",
        "",
        "## Scene envelope",
        "",
        f"- scene index: `{sel['scene_index']}`",
        f"- restore score: `{sel['restore_score']}`",
        f"- peak memory: `{sel['peak_memory_bytes']}`",
        f"- union rect: `{spec['restore_template']['union_rect']}`",
        f"- unique rects: `{len(spec['restore_template']['unique_rects'])}`",
        f"- TTM owners: `{', '.join(spec['restore_template']['ttm_names'])}`",
        "",
        "## Scene resources",
        "",
        f"- BMPs: `{', '.join(spec['scene_resources']['bmps'])}`",
        f"- SCRs: `{', '.join(spec['scene_resources']['scrs'])}`",
        f"- TTMs: `{', '.join(spec['scene_resources']['ttms'])}`",
        "",
        "## Runtime scope",
        "",
    ]
    for note in spec["recommended_runtime_scope"]["notes"]:
        lines.append(f"- {note}")
    lines.extend(["", "## TTM details", ""])
    for row in spec["ttm_details"]:
        lines.append(
            f"- `{row['ttm_name']}`: rect `{row['union_rect']}`, clear regions `{row['clear_region_ids']}`, op counts `{row['op_counts']}`"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    ap.add_argument("--manifest-dir", type=Path, default=DEFAULT_MANIFEST_DIR)
    ap.add_argument("--template-dir", type=Path, default=DEFAULT_TEMPLATE_DIR)
    ap.add_argument("--scene-analysis", type=Path, default=DEFAULT_SCENE_ANALYSIS)
    ap.add_argument("--ads-name", default=None, help="pick a specific ADS family from recommended_pilots")
    ap.add_argument("--all-recommended", action="store_true", help="emit one spec per recommended pilot")
    ap.add_argument(
        "--all-scenes",
        action="store_true",
        help="emit one spec per ranked scene from all_candidates",
    )
    ap.add_argument("--limit", type=int, default=None, help="limit number of recommended pilots when batching")
    ap.add_argument("--output-dir", type=Path, default=None, help="directory for batched spec outputs")
    ap.add_argument("--json-output", type=Path, default=DEFAULT_JSON_OUTPUT)
    ap.add_argument("--md-output", type=Path, default=DEFAULT_MD_OUTPUT)
    return ap


def main() -> int:
    args = build_parser().parse_args()
    report = load_json(args.report)
    scene_analysis = load_json(args.scene_analysis)

    if args.all_recommended or args.all_scenes:
        output_dir = args.output_dir
        if output_dir is None:
            raise SystemExit("--all-recommended/--all-scenes requires --output-dir")

        candidate_set = "all_candidates" if args.all_scenes else "recommended_pilots"
        candidates = select_candidates(report, args.ads_name, args.limit, candidate_set)
        specs = []
        for candidate in candidates:
            spec = build_spec(candidate, args.manifest_dir, args.template_dir, scene_analysis)
            slug = slugify_scene(candidate["ads_name"], candidate["ads_tag"]) if args.all_scenes else slugify_ads_name(candidate["ads_name"])
            write_json(output_dir / f"{slug}.json", spec)
            write_markdown(output_dir / f"{slug}.md", spec)
            specs.append(spec)

        summary = build_batch_summary(specs, output_dir, args.all_scenes)
        write_json(output_dir / "summary.json", summary)
        write_batch_markdown(output_dir / "summary.md", summary)
        print(
            json.dumps(
                {
                    "output_dir": str(output_dir),
                    "pilot_count": summary["pilot_count"],
                    "pilots": [
                        {
                            "ads_name": row["ads_name"],
                            "ads_tag": row["ads_tag"],
                            "scene_index": row["scene_index"],
                        }
                        for row in summary["pilots"]
                    ],
                },
                indent=2,
            )
        )
        return 0

    candidates = select_candidates(report, args.ads_name, 1, "recommended_pilots")
    if not candidates:
        raise SystemExit("No matching recommended pilot found")
    spec = build_spec(candidates[0], args.manifest_dir, args.template_dir, scene_analysis)
    write_json(args.json_output, spec)
    write_markdown(args.md_output, spec)
    print(
        json.dumps(
            {
                "json_output": str(args.json_output),
                "md_output": str(args.md_output),
                "selected_scene": spec["selected_scene"],
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
