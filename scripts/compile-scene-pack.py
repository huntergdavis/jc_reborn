#!/usr/bin/env python3
"""Compile one or more scene-pack manifests into self-describing pack payloads."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple


DEFAULT_MANIFEST = Path("docs/ps1/research/scene_pack_manifests_2026-03-17/building-ads.json")
DEFAULT_MANIFEST_DIR = Path("docs/ps1/research/scene_pack_manifests_2026-03-17")
DEFAULT_EXTRACTED_ROOT = Path("jc_resources/extracted")
DEFAULT_OUTPUT_ROOT = Path("docs/ps1/research/compiled_packs_2026-03-17")
DEFAULT_STAGE_ROOT = Path("jc_resources/packs")
DEFAULT_TEMPLATE_ROOT = Path("docs/ps1/research/dirty_region_templates_2026-03-18")
DEFAULT_ALIGNMENT = 2048
PACK_MAGIC = 0x4B415053  # "SPAK" little-endian
PACK_VERSION = 1
PACK_NAME_BYTES = 16
PACK_PREFETCH_MAX = 1

RESOURCE_DIRS = {
    "ads": "ads",
    "bmps": "bmp",
    "scrs": "scr",
    "ttms": "ttm",
}

RESOURCE_TYPES = {
    "ads": "ads",
    "bmps": "bmp",
    "scrs": "scr",
    "ttms": "ttm",
}

RESOURCE_ORDER = ("ads", "scrs", "ttms", "bmps")
ISLAND_SHARED_RESOURCES: Sequence[Tuple[str, str]] = (
    ("scrs", "OCEAN00.SCR"),
    ("scrs", "OCEAN01.SCR"),
    ("scrs", "OCEAN02.SCR"),
    ("scrs", "NIGHT.SCR"),
    ("bmps", "MRAFT.BMP"),
    ("bmps", "BACKGRND.BMP"),
    ("bmps", "HOLIDAY.BMP"),
)


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def align_up(value: int, alignment: int) -> int:
    if alignment <= 0:
        raise ValueError("alignment must be positive")
    return ((value + alignment - 1) // alignment) * alignment


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def make_pack_filename(manifest: dict) -> str:
    cluster_key = manifest.get("cluster_key") or manifest["pack_id"]
    stem = Path(cluster_key).stem.upper()
    return f"{stem}.PAK"


def resource_type_code(resource_type: str) -> int:
    codes = {"ads": 1, "scr": 2, "ttm": 3, "bmp": 4}
    return codes[resource_type]


def encode_name(name: str) -> bytes:
    data = name.encode("ascii")
    if len(data) >= PACK_NAME_BYTES:
        raise ValueError(f"resource name too long for pack header: {name}")
    return data + (b"\x00" * (PACK_NAME_BYTES - len(data)))


def collect_resource_entries(manifest: dict, extracted_root: Path) -> List[Tuple[str, str, Path]]:
    entries: List[Tuple[str, str, Path]] = []
    seen: set[Tuple[str, str]] = set()
    resources = manifest.get("resources", {})
    scenes = manifest.get("scenes", [])
    has_island_scene = any("ISLAND" in scene.get("flags", []) for scene in scenes)

    for kind in RESOURCE_ORDER:
        for name in resources.get(kind, []):
            source_path = extracted_root / RESOURCE_DIRS[kind] / name
            if not source_path.is_file():
                raise FileNotFoundError(f"missing resource for {kind}:{name}: {source_path}")
            key = (RESOURCE_TYPES[kind], name)
            if key not in seen:
                entries.append((RESOURCE_TYPES[kind], name, source_path))
                seen.add(key)

    if has_island_scene:
        for kind, name in ISLAND_SHARED_RESOURCES:
            source_path = extracted_root / RESOURCE_DIRS[kind] / name
            if not source_path.is_file():
                raise FileNotFoundError(f"missing shared island resource for {kind}:{name}: {source_path}")
            key = (RESOURCE_TYPES[kind], name)
            if key not in seen:
                entries.append((RESOURCE_TYPES[kind], name, source_path))
                seen.add(key)

    return entries


def compile_pack(
    manifest: dict,
    manifest_path: Path,
    extracted_root: Path,
    output_root: Path,
    stage_root: Path,
    template_root: Path,
    alignment: int,
) -> dict:
    entries = collect_resource_entries(manifest, extracted_root)
    output_dir = output_root / manifest["pack_id"]
    payload_path = output_dir / "pack_payload.bin"
    index_path = output_dir / "pack_index.json"
    dirty_template_path = output_dir / "dirty_region_templates.json"
    stage_filename = make_pack_filename(manifest)
    stage_path = stage_root / stage_filename
    source_template_path = template_root / f"{manifest['pack_id']}.json"
    dirty_template = load_json(source_template_path) if source_template_path.is_file() else None

    output_dir.mkdir(parents=True, exist_ok=True)
    stage_root.mkdir(parents=True, exist_ok=True)

    entry_struct = struct.Struct(f"<BBHII{PACK_NAME_BYTES}s")
    prefetch_names = manifest.get("ads_names", [])
    prefetch_pack_ids = manifest.get("transition_hints", {}).get("likely_next_pack_ids", [])
    prefetch_ads_names = [f"{pack_id.split('-', 1)[0].upper()}.ADS" for pack_id in prefetch_pack_ids[:PACK_PREFETCH_MAX]]
    header_struct = struct.Struct("<IIIII")
    header_size = (
        header_struct.size
        + (len(entries) * entry_struct.size)
        + (PACK_PREFETCH_MAX * PACK_NAME_BYTES)
    )
    first_resource_offset = align_up(header_size, alignment)

    offset = first_resource_offset
    total_padding = 0
    compiled_entries = []

    with payload_path.open("wb") as payload:
        payload.write(b"\x00" * first_resource_offset)
        for resource_type, name, source_path in entries:
            aligned_offset = align_up(offset, alignment)
            padding = aligned_offset - offset
            if padding:
                payload.write(b"\x00" * padding)
                total_padding += padding
                offset = aligned_offset

            data = source_path.read_bytes()
            payload.write(data)

            aligned_size = align_up(len(data), alignment)
            trailing_padding = aligned_size - len(data)
            if trailing_padding:
                payload.write(b"\x00" * trailing_padding)
                total_padding += trailing_padding

            compiled_entries.append(
                {
                    "resource_type": resource_type,
                    "name": name,
                    "source_path": str(source_path),
                    "offset_bytes": offset,
                    "size_bytes": len(data),
                    "aligned_size_bytes": aligned_size,
                    "sector_index": offset // alignment,
                    "sector_count": aligned_size // alignment,
                    "sha256": sha256_bytes(data),
                }
            )
            offset += aligned_size

    with payload_path.open("r+b") as payload:
        payload.seek(0)
        payload.write(
            header_struct.pack(
                PACK_MAGIC,
                PACK_VERSION,
                len(compiled_entries),
                first_resource_offset,
                len(prefetch_ads_names),
            )
        )
        for entry in compiled_entries:
            payload.write(
                entry_struct.pack(
                    resource_type_code(entry["resource_type"]),
                    0,
                    0,
                    entry["offset_bytes"],
                    entry["size_bytes"],
                    encode_name(entry["name"]),
                )
            )
        for name in prefetch_ads_names:
            payload.write(encode_name(name))
        for _ in range(PACK_PREFETCH_MAX - len(prefetch_ads_names)):
            payload.write(b"\x00" * PACK_NAME_BYTES)
        payload.write(b"\x00" * (first_resource_offset - header_size))

    stage_path.write_bytes(payload_path.read_bytes())

    index = {
        "schema_version": 1,
        "artifact_kind": "compiled_scene_pack",
        "pack_id": manifest["pack_id"],
        "source_manifest": str(manifest_path),
        "pack_strategy": manifest.get("pack_strategy"),
        "cluster_key": manifest.get("cluster_key"),
        "ads_names": manifest.get("ads_names", []),
        "scene_indices": manifest.get("scene_indices", []),
        "runtime_requirements": manifest.get("runtime_requirements", {}),
        "transition_hints": manifest.get("transition_hints", {}),
        "prefetch_hints": manifest.get("prefetch_hints", {}),
        "dirty_region_templates": {
            "path": str(dirty_template_path),
            "source_path": str(source_template_path) if dirty_template is not None else None,
            "available": dirty_template is not None,
            "candidate_scene_indices": (
                dirty_template.get("summary", {}).get("candidate_scene_indices", [])
                if dirty_template is not None else []
            ),
            "restore_candidate_ttm_count": (
                dirty_template.get("summary", {}).get("restore_candidate_ttm_count", 0)
                if dirty_template is not None else 0
            ),
        },
        "payload": {
            "path": str(payload_path),
            "alignment_bytes": alignment,
            "payload_bytes": payload_path.stat().st_size,
            "resource_count": len(compiled_entries),
            "padding_bytes": total_padding,
            "header_bytes": header_size,
            "first_resource_offset": first_resource_offset,
            "staged_path": str(stage_path),
            "staged_filename": stage_filename,
            "prefetch_ads_names": prefetch_ads_names,
        },
        "entries": compiled_entries,
    }

    if dirty_template is not None:
        dirty_template_path.write_text(json.dumps(dirty_template, indent=2) + "\n", encoding="utf-8")
    index_path.write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")
    return index


def compile_many(
    manifest_paths: Iterable[Path],
    extracted_root: Path,
    output_root: Path,
    stage_root: Path,
    template_root: Path,
    alignment: int,
) -> List[dict]:
    indices = []
    for manifest_path in manifest_paths:
        manifest = load_json(manifest_path)
        index = compile_pack(
            manifest=manifest,
            manifest_path=manifest_path,
            extracted_root=extracted_root,
            output_root=output_root,
            stage_root=stage_root,
            template_root=template_root,
            alignment=alignment,
        )
        indices.append(index)

    indices.sort(key=lambda item: item["pack_id"])
    return indices


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST, help="single scene pack manifest JSON")
    ap.add_argument("--manifest-dir", type=Path, default=DEFAULT_MANIFEST_DIR, help="directory of manifest JSON files")
    ap.add_argument("--all", action="store_true", help="compile every manifest in --manifest-dir")
    ap.add_argument("--extracted-root", type=Path, default=DEFAULT_EXTRACTED_ROOT, help="root of extracted resource files")
    ap.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT, help="root directory for compiled pack payloads")
    ap.add_argument("--stage-root", type=Path, default=DEFAULT_STAGE_ROOT, help="directory for staged CD-visible pack payloads")
    ap.add_argument("--template-root", type=Path, default=DEFAULT_TEMPLATE_ROOT, help="directory of offline dirty-region template JSON files")
    ap.add_argument("--alignment", type=int, default=DEFAULT_ALIGNMENT, help="resource alignment in bytes")
    return ap


def main() -> int:
    args = build_parser().parse_args()

    if args.all:
        manifest_paths = sorted(args.manifest_dir.glob("*.json"))
        if not manifest_paths:
            raise FileNotFoundError(f"no manifests found in {args.manifest_dir}")
    else:
        manifest_paths = [args.manifest]

    indices = compile_many(
        manifest_paths=manifest_paths,
        extracted_root=args.extracted_root,
        output_root=args.output_root,
        stage_root=args.stage_root,
        template_root=args.template_root,
        alignment=args.alignment,
    )

    print(
        json.dumps(
            {
                "compiled_pack_count": len(indices),
                "pack_ids": [index["pack_id"] for index in indices],
                "staged_payloads": [index["payload"]["staged_filename"] for index in indices],
                "output_root": str(args.output_root),
                "stage_root": str(args.stage_root),
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
