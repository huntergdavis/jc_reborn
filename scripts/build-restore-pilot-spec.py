#!/usr/bin/env python3
"""Build a scene-level restore pilot spec from ranked candidates and templates."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


DEFAULT_REPORT = Path("docs/ps1/research/restore_candidate_report_2026-03-18.json")
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
    report: dict,
    manifest_dir: Path,
    template_dir: Path,
    scene_analysis: dict,
    ads_name: str | None,
) -> dict:
    if ads_name is None:
        candidate = report["recommended_pilots"][0]
    else:
        candidate = next(row for row in report["recommended_pilots"] if row["ads_name"] == ads_name)

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
    ap.add_argument("--json-output", type=Path, default=DEFAULT_JSON_OUTPUT)
    ap.add_argument("--md-output", type=Path, default=DEFAULT_MD_OUTPUT)
    return ap


def main() -> int:
    args = build_parser().parse_args()
    report = load_json(args.report)
    scene_analysis = load_json(args.scene_analysis)
    spec = build_spec(report, args.manifest_dir, args.template_dir, scene_analysis, args.ads_name)
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
