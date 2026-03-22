#!/usr/bin/env python3
"""Extract per-pack dirty-region template candidates from TTM bytecode."""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path
from typing import Dict, Iterable, List, Mapping, Sequence, Tuple


DEFAULT_MANIFEST = Path("docs/ps1/research/generated/scene_pack_manifests_2026-03-21/building-ads.json")
DEFAULT_MANIFEST_DIR = Path("docs/ps1/research/generated/scene_pack_manifests_2026-03-21")
DEFAULT_EXTRACTED_ROOT = Path("jc_resources/extracted")
DEFAULT_OUTPUT_ROOT = Path("docs/ps1/research/generated/dirty_region_templates_2026-03-21")
SCHEMA_VERSION = 1
TTM_TAG = 0x1111
TTM_LOCAL_TAG = 0x1101

DIRTY_REGION_OPS = {
    0x1121: "region_id",
    0x4204: "copy_zone_to_bg",
    0x4214: "save_image1",
    0xA054: "save_zone",
    0xA064: "restore_zone",
    0xA601: "clear_screen",
}

SCENE_WIDTH = 640
SCENE_HEIGHT = 350


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def peek_u16(data: bytes, offset: int) -> Tuple[int, int]:
    return struct.unpack_from("<H", data, offset)[0], offset + 2


def as_s16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def read_name(data: bytes, offset: int) -> Tuple[str, int]:
    end = offset
    while end < len(data) and data[end] != 0:
        end += 1
    raw = data[offset:end]
    end += 1
    if (end - offset) & 1:
        end += 1
    return raw.decode("ascii", errors="replace"), end


def rect_copy(rect: Mapping[str, int]) -> Dict[str, int]:
    return {
        "x": rect["x"],
        "y": rect["y"],
        "width": rect["width"],
        "height": rect["height"],
    }


def unique_rects(rects: Sequence[Mapping[str, int]]) -> List[Dict[str, int]]:
    unique = []
    seen = set()
    for rect in rects:
        key = (rect["x"], rect["y"], rect["width"], rect["height"])
        if key in seen:
            continue
        seen.add(key)
        unique.append(rect_copy(rect))
    return unique


def union_rects(rects: Sequence[Mapping[str, int]]) -> Dict[str, int] | None:
    if not rects:
        return None
    x1 = min(rect["x"] for rect in rects)
    y1 = min(rect["y"] for rect in rects)
    x2 = max(rect["x"] + rect["width"] for rect in rects)
    y2 = max(rect["y"] + rect["height"] for rect in rects)
    return {"x": x1, "y": y1, "width": x2 - x1, "height": y2 - y1}


def normalized_rect(args: Sequence[int]) -> Dict[str, int] | None:
    if len(args) < 4:
        return None

    x = as_s16(args[0])
    y = as_s16(args[1])
    width = int(args[2])
    height = int(args[3])

    if width <= 0 or height <= 0:
        return None

    x2 = x + width
    y2 = y + height

    if x2 <= 0 or y2 <= 0:
        return None
    if x >= SCENE_WIDTH or y >= SCENE_HEIGHT:
        return None

    x = max(0, x)
    y = max(0, y)
    x2 = min(SCENE_WIDTH, x2)
    y2 = min(SCENE_HEIGHT, y2)

    if x2 <= x or y2 <= y:
        return None

    return {"x": x, "y": y, "width": x2 - x, "height": y2 - y}


def make_tag_entry(tag: int) -> dict:
    return {
        "tag": tag,
        "region_ids": [],
        "clear_region_ids": [],
        "load_screens": [],
        "update_count": 0,
        "rects": [],
    }


def parse_ttm_template(ttm_name: str, path: Path) -> dict:
    data = path.read_bytes()
    offset = 0
    current_tag = -1
    current_region_id = None
    tag_map: Dict[int, dict] = {}
    op_counts = {name: 0 for name in DIRTY_REGION_OPS.values()}
    all_rects: List[Dict[str, int]] = []

    while offset + 1 < len(data):
        opcode, offset = peek_u16(data, offset)
        num_args = opcode & 0x000F
        args: List[int] = []
        str_arg = None

        if num_args == 0x0F:
            str_arg, offset = read_name(data, offset)
        else:
            for _ in range(num_args):
                arg, offset = peek_u16(data, offset)
                args.append(arg)

        if opcode in (TTM_TAG, TTM_LOCAL_TAG):
            current_tag = args[0] if args else -1
            tag_map.setdefault(current_tag, make_tag_entry(current_tag))
            continue

        tag_entry = tag_map.setdefault(current_tag, make_tag_entry(current_tag))

        if opcode == 0x0FF0:
            tag_entry["update_count"] += 1
            continue

        if opcode == 0xF01F and str_arg is not None:
            tag_entry["load_screens"].append(str_arg)
            continue

        if opcode not in DIRTY_REGION_OPS:
            continue

        op_name = DIRTY_REGION_OPS[opcode]
        op_counts[op_name] += 1

        if opcode == 0x1121:
            current_region_id = args[0] if args else None
            if current_region_id is not None and current_region_id not in tag_entry["region_ids"]:
                tag_entry["region_ids"].append(current_region_id)
            continue

        if opcode == 0xA601:
            region_id = args[0] if args else 0
            tag_entry["clear_region_ids"].append(region_id)
            continue

        rect = normalized_rect(args)
        if rect is None:
            continue
        tag_entry["rects"].append(rect)
        all_rects.append(rect)

    tag_templates = []
    stable_tag_count = 0
    for tag in sorted(tag_map):
        entry = tag_map[tag]
        uniq = unique_rects(entry["rects"])
        stable_candidate = bool(uniq) and entry["update_count"] <= 1
        if stable_candidate:
            stable_tag_count += 1
        tag_templates.append(
            {
                "tag": entry["tag"],
                "update_count": entry["update_count"],
                "region_ids": sorted(set(entry["region_ids"])),
                "clear_region_ids": sorted(set(entry["clear_region_ids"])),
                "load_screens": entry["load_screens"],
                "unique_rect_count": len(uniq),
                "unique_rects": uniq,
                "union_rect": union_rects(uniq),
                "stable_candidate": stable_candidate,
            }
        )

    uniq_all = unique_rects(all_rects)
    restore_ops = (
        op_counts["copy_zone_to_bg"]
        + op_counts["save_image1"]
        + op_counts["save_zone"]
        + op_counts["restore_zone"]
    )
    unique_clear_regions = sorted(
        {
            region_id
            for entry in tag_map.values()
            for region_id in entry["clear_region_ids"]
        }
    )
    unique_region_ids = sorted(
        {
            region_id
            for entry in tag_map.values()
            for region_id in entry["region_ids"]
        }
    )
    clear_heavy = len(unique_clear_regions) > max(2, len(uniq_all))
    small_region_set = 0 < len(uniq_all) <= 8
    return {
        "ttm_name": ttm_name,
        "byte_size": len(data),
        "op_counts": op_counts,
        "unique_rect_count": len(uniq_all),
        "unique_rects": uniq_all,
        "union_rect": union_rects(uniq_all),
        "unique_region_ids": unique_region_ids,
        "clear_heavy": clear_heavy,
        "unique_clear_region_ids": unique_clear_regions,
        "restore_candidate": restore_ops > 0 and stable_tag_count > 0 and small_region_set,
        "tag_templates": tag_templates,
    }


def build_pack_templates(manifest: dict, extracted_root: Path) -> dict:
    ttm_templates = []
    scene_templates: Dict[int, dict] = {}
    candidate_scene_indices = set()

    for detail in manifest.get("resource_details", {}).get("ttms", []):
        ttm_name = detail["name"]
        template = parse_ttm_template(ttm_name, extracted_root / "ttm" / ttm_name)
        template["source_scene_indices"] = detail.get("source_scene_indices", [])
        template["slot_ids"] = detail.get("metadata", {}).get("slot_ids", [])
        ttm_templates.append(template)

        for scene_index in detail.get("source_scene_indices", []):
            scene_entry = scene_templates.setdefault(
                scene_index,
                {
                    "scene_index": scene_index,
                    "ttm_names": [],
                    "unique_rects": [],
                    "clear_screen_count": 0,
                    "restore_candidate": False,
                },
            )
            scene_entry["ttm_names"].append(ttm_name)
            scene_entry["clear_screen_count"] += template["op_counts"]["clear_screen"]
            scene_entry["restore_candidate"] = scene_entry["restore_candidate"] or template["restore_candidate"]
            for rect in template["unique_rects"]:
                if rect not in scene_entry["unique_rects"]:
                    scene_entry["unique_rects"].append(rect)
            if template["restore_candidate"]:
                candidate_scene_indices.add(scene_index)

    scene_rows = []
    for scene_index in sorted(scene_templates):
        scene_entry = scene_templates[scene_index]
        scene_entry["ttm_names"].sort()
        scene_entry["unique_rect_count"] = len(scene_entry["unique_rects"])
        scene_entry["union_rect"] = union_rects(scene_entry["unique_rects"])
        scene_rows.append(scene_entry)

    return {
        "schema_version": SCHEMA_VERSION,
        "artifact_kind": "dirty_region_template_pack",
        "pack_id": manifest["pack_id"],
        "ads_names": manifest.get("ads_names", []),
        "scene_indices": manifest.get("scene_indices", []),
        "summary": {
            "ttm_count": len(ttm_templates),
            "restore_candidate_ttm_count": sum(1 for row in ttm_templates if row["restore_candidate"]),
            "scene_template_count": len(scene_rows),
            "candidate_scene_indices": sorted(candidate_scene_indices),
        },
        "ttm_templates": ttm_templates,
        "scene_templates": scene_rows,
    }


def build_aggregate(templates: Sequence[dict]) -> dict:
    candidate_pack_ids = []
    candidate_scene_total = 0
    for template in templates:
        if template["summary"]["candidate_scene_indices"]:
            candidate_pack_ids.append(template["pack_id"])
            candidate_scene_total += len(template["summary"]["candidate_scene_indices"])

    return {
        "schema_version": SCHEMA_VERSION,
        "artifact_kind": "dirty_region_template_summary",
        "pack_count": len(templates),
        "candidate_pack_ids": candidate_pack_ids,
        "candidate_scene_total": candidate_scene_total,
        "packs": [
            {
                "pack_id": template["pack_id"],
                "ads_names": template["ads_names"],
                "candidate_scene_indices": template["summary"]["candidate_scene_indices"],
                "restore_candidate_ttm_count": template["summary"]["restore_candidate_ttm_count"],
                "scene_template_count": template["summary"]["scene_template_count"],
            }
            for template in templates
        ],
    }


def iter_manifest_paths(args: argparse.Namespace) -> Iterable[Path]:
    if args.all:
        manifest_paths = sorted(args.manifest_dir.glob("*.json"))
        if not manifest_paths:
            raise FileNotFoundError(f"no manifests found in {args.manifest_dir}")
        return manifest_paths
    return [args.manifest]


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST, help="single scene pack manifest JSON")
    ap.add_argument("--manifest-dir", type=Path, default=DEFAULT_MANIFEST_DIR, help="directory of manifest JSON files")
    ap.add_argument("--all", action="store_true", help="extract templates for every manifest in --manifest-dir")
    ap.add_argument("--extracted-root", type=Path, default=DEFAULT_EXTRACTED_ROOT, help="root of extracted resource files")
    ap.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT, help="directory for per-pack template JSON outputs")
    return ap


def main() -> int:
    args = build_parser().parse_args()
    manifest_paths = iter_manifest_paths(args)
    pack_templates = []

    for manifest_path in manifest_paths:
        manifest = load_json(manifest_path)
        template = build_pack_templates(manifest, args.extracted_root)
        pack_templates.append(template)
        write_json(args.output_root / f"{manifest['pack_id']}.json", template)

    pack_templates.sort(key=lambda item: item["pack_id"])
    summary = build_aggregate(pack_templates)
    write_json(args.output_root / "summary.json", summary)
    print(
        json.dumps(
            {
                "pack_count": len(pack_templates),
                "output_root": str(args.output_root),
                "candidate_pack_ids": summary["candidate_pack_ids"],
                "candidate_scene_total": summary["candidate_scene_total"],
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
