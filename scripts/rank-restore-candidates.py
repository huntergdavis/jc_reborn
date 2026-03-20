#!/usr/bin/env python3
"""Rank scene-level restore candidates from pack manifests and dirty templates."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Dict, List


DEFAULT_MANIFEST_DIR = Path("docs/ps1/research/scene_pack_manifests_2026-03-17")
DEFAULT_TEMPLATE_DIR = Path("docs/ps1/research/dirty_region_templates_2026-03-18")
DEFAULT_JSON_OUTPUT = Path("docs/ps1/research/restore_candidate_report_2026-03-18.json")
DEFAULT_MD_OUTPUT = Path("docs/ps1/research/restore_candidate_report_2026-03-18.md")


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def score_scene(scene: dict, template: dict) -> int:
    score = 0
    peak_memory = int(scene["memory"]["peak_memory_bytes"])
    bmp_count = int(scene["resource_counts"]["bmps"])
    unique_rect_count = int(template["unique_rect_count"])
    clear_count = int(template["clear_screen_count"])

    if template["restore_candidate"]:
        score += 50
    score += max(0, 24 - min(unique_rect_count, 24))
    score += max(0, 12 - min(bmp_count, 12))
    score += max(0, 32 - min(peak_memory // 16384, 32))
    score += max(0, 16 - min(clear_count // 128, 16))
    return score


def build_rows(manifest_dir: Path, template_dir: Path) -> List[dict]:
    rows: List[dict] = []
    for manifest_path in sorted(manifest_dir.glob("*.json")):
        template_path = template_dir / manifest_path.name
        if not template_path.is_file():
            continue

        manifest = load_json(manifest_path)
        template = load_json(template_path)
        scene_by_index: Dict[int, dict] = {row["scene_index"]: row for row in manifest.get("scenes", [])}

        for scene_template in template.get("scene_templates", []):
            scene_index = int(scene_template["scene_index"])
            scene = scene_by_index.get(scene_index)
            if scene is None:
                continue

            row = {
                "pack_id": manifest["pack_id"],
                "ads_name": scene["ads_name"],
                "ads_tag": scene["ads_tag"],
                "scene_index": scene_index,
                "peak_memory_bytes": scene["memory"]["peak_memory_bytes"],
                "bmp_count": scene["resource_counts"]["bmps"],
                "ttm_count": scene["resource_counts"]["ttms"],
                "restore_candidate": scene_template["restore_candidate"],
                "clear_screen_count": scene_template["clear_screen_count"],
                "unique_rect_count": scene_template["unique_rect_count"],
                "union_rect": scene_template["union_rect"],
                "ttm_names": scene_template["ttm_names"],
            }
            row["restore_score"] = score_scene(scene, scene_template)
            rows.append(row)

    rows.sort(
        key=lambda item: (
            -item["restore_score"],
            item["peak_memory_bytes"],
            item["unique_rect_count"],
            item["pack_id"],
            item["ads_tag"],
        )
    )
    return rows


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def write_markdown(path: Path, payload: dict) -> None:
    lines = [
        "# Restore Candidate Report",
        "",
        "Date: 2026-03-18",
        "",
        "Top scene-level Phase 7 candidates ranked from pack manifests plus dirty-region templates.",
        "",
        "## Top candidates",
        "",
    ]

    for row in payload["top_candidates"]:
        lines.append(
            f"- `{row['pack_id']}` scene `{row['scene_index']}` = `{row['ads_name']} tag {row['ads_tag']}`"
            f" score `{row['restore_score']}` peak `{row['peak_memory_bytes']}`"
            f" rects `{row['unique_rect_count']}` bmps `{row['bmp_count']}`"
        )

    lines.extend(
        [
            "",
            "## Recommended pilot picks",
            "",
        ]
    )
    for row in payload["recommended_pilots"]:
        lines.append(
            f"- `{row['ads_name']} tag {row['ads_tag']}`"
            f" from `{row['pack_id']}`: scene `{row['scene_index']}`,"
            f" score `{row['restore_score']}`, peak `{row['peak_memory_bytes']}`,"
            f" rect union `{row['union_rect']}`"
        )

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def build_report(rows: List[dict]) -> dict:
    return build_report_with_limit(rows, 5)


def build_report_with_limit(rows: List[dict], recommended_limit: int) -> dict:
    recommended = []
    seen_ads = set()
    for row in rows:
        key = row["ads_name"]
        if key in seen_ads:
            continue
        seen_ads.add(key)
        recommended.append(row)
        if len(recommended) >= recommended_limit:
            break

    return {
        "schema_version": 1,
        "artifact_kind": "restore_candidate_report",
        "scene_count": len(rows),
        "all_candidates": rows,
        "top_candidates": rows[:12],
        "recommended_pilots": recommended,
    }


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--manifest-dir", type=Path, default=DEFAULT_MANIFEST_DIR)
    ap.add_argument("--template-dir", type=Path, default=DEFAULT_TEMPLATE_DIR)
    ap.add_argument("--json-output", type=Path, default=DEFAULT_JSON_OUTPUT)
    ap.add_argument("--md-output", type=Path, default=DEFAULT_MD_OUTPUT)
    ap.add_argument("--recommended-limit", type=int, default=5, help="max unique ADS families to emit in recommended_pilots")
    return ap


def main() -> int:
    args = build_parser().parse_args()
    rows = build_rows(args.manifest_dir, args.template_dir)
    report = build_report_with_limit(rows, args.recommended_limit)
    write_json(args.json_output, report)
    write_markdown(args.md_output, report)
    print(
        json.dumps(
            {
                "scene_count": report["scene_count"],
                "json_output": str(args.json_output),
                "md_output": str(args.md_output),
                "top_candidate": report["top_candidates"][0] if report["top_candidates"] else None,
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
