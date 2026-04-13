#!/usr/bin/env python3

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image

def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_result(path: Path) -> dict:
    with path.open(encoding="utf-8") as f:
        return json.load(f)


def listed_files(directory: Path, pattern: str) -> list[Path]:
    if not directory or not directory.is_dir():
        return []
    return sorted(p for p in directory.glob(pattern) if p.is_file())


def resolve_dump_dir(path_value: str | None) -> Path | None:
    if not path_value:
        return None
    candidate = Path(path_value)
    if not candidate.exists():
        return candidate
    if any(candidate.glob("cpu_to_vram_copy_*.png")):
        return candidate
    jcreborn = candidate / "jcreborn"
    if jcreborn.is_dir() and any(jcreborn.glob("cpu_to_vram_copy_*.png")):
        return jcreborn
    return candidate


def compare_named_sets(base_dir: Path, overlay_dir: Path, pattern: str) -> dict:
    base_files = {p.name: p for p in listed_files(base_dir, pattern)}
    overlay_files = {p.name: p for p in listed_files(overlay_dir, pattern)}
    common = sorted(set(base_files) & set(overlay_files))
    base_only = sorted(set(base_files) - set(overlay_files))
    overlay_only = sorted(set(overlay_files) - set(base_files))
    first_diff = None
    compared = 0

    for name in common:
        compared += 1
        if sha256(base_files[name]) != sha256(overlay_files[name]):
            first_diff = name
            break

    return {
        "pattern": pattern,
        "base_dir": str(base_dir) if base_dir else None,
        "overlay_dir": str(overlay_dir) if overlay_dir else None,
        "common_count": len(common),
        "base_only": base_only,
        "overlay_only": overlay_only,
        "first_diff": first_diff,
        "all_common_identical": first_diff is None,
        "common_files": common,
        "compared_files": compared,
    }


def compare_selected_files(base_dir: Path, overlay_dir: Path, files: list[str]) -> dict:
    first_diff = None
    compared = 0
    for name in files:
        compared += 1
        if sha256(base_dir / name) != sha256(overlay_dir / name):
            first_diff = name
            break

    return {
        "base_dir": str(base_dir) if base_dir else None,
        "overlay_dir": str(overlay_dir) if overlay_dir else None,
        "common_count": len(files),
        "first_diff": first_diff,
        "all_common_identical": first_diff is None,
        "common_files": files,
        "compared_files": compared,
    }


def frame_black_stats(directory: Path, pattern: str = "frame_*.png") -> dict:
    files = listed_files(directory, pattern)
    black_files: list[str] = []
    max_levels: dict[str, int] = {}

    for path in files:
        img = Image.open(path).convert("RGB")
        max_level = 0
        for r, g, b in img.getdata():
            level = max(r, g, b)
            if level > max_level:
                max_level = level
        max_levels[path.name] = max_level
        if max_level == 0:
            black_files.append(path.name)

    return {
        "directory": str(directory) if directory else None,
        "frame_count": len(files),
        "all_black_count": len(black_files),
        "all_black_files": black_files,
        "max_level_by_file": max_levels,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compare two regtest-scene result bundles, including filtered frames and CPU->VRAM dumps."
    )
    parser.add_argument("--base", required=True, help="Path to baseline result.json")
    parser.add_argument("--overlay", required=True, help="Path to overlay/variant result.json")
    args = parser.parse_args()

    base_path = Path(args.base).resolve()
    overlay_path = Path(args.overlay).resolve()
    base = load_result(base_path)
    overlay = load_result(overlay_path)

    base_filtered = Path(base["paths"]["filtered_frames_dir"] or base["paths"]["frames_dir"])
    overlay_filtered = Path(overlay["paths"]["filtered_frames_dir"] or overlay["paths"]["frames_dir"])

    base_raw = resolve_dump_dir(base["paths"].get("cpu_to_vram_copy_dir") or base["paths"].get("raw_frames_dir"))
    overlay_raw = resolve_dump_dir(overlay["paths"].get("cpu_to_vram_copy_dir") or overlay["paths"].get("raw_frames_dir"))

    filtered_cmp = compare_named_sets(base_filtered, overlay_filtered, "frame_*.png")
    base_black = frame_black_stats(base_filtered)
    overlay_black = frame_black_stats(overlay_filtered)
    visible_common = [
        name for name in filtered_cmp["common_files"]
        if base_black["max_level_by_file"].get(name, 0) > 0
        and overlay_black["max_level_by_file"].get(name, 0) > 0
    ]
    visible_cmp = compare_selected_files(base_filtered, overlay_filtered, visible_common)
    vram_cmp = compare_named_sets(
        base_raw,
        overlay_raw,
        "cpu_to_vram_copy_*.png",
    )

    payload = {
        "base_result": str(base_path),
        "overlay_result": str(overlay_path),
        "scene": {
            "base_boot": base["scene"]["boot_string"],
            "overlay_boot": overlay["scene"]["boot_string"],
        },
        "outcome": {
            "filtered_frames_equal": filtered_cmp["all_common_identical"]
            and not filtered_cmp["base_only"]
            and not filtered_cmp["overlay_only"],
            "cpu_to_vram_dumps_equal": vram_cmp["all_common_identical"]
            and not vram_cmp["base_only"]
            and not vram_cmp["overlay_only"],
            "state_hash_equal": base["outcome"].get("state_hash") == overlay["outcome"].get("state_hash"),
            "save_state_hash_equal": base["outcome"].get("raw_hashes", {}).get("save_state_hash")
            == overlay["outcome"].get("raw_hashes", {}).get("save_state_hash"),
            "ram_hash_equal": base["outcome"].get("raw_hashes", {}).get("ram_hash")
            == overlay["outcome"].get("raw_hashes", {}).get("ram_hash"),
            "vram_hash_equal": base["outcome"].get("raw_hashes", {}).get("vram_hash")
            == overlay["outcome"].get("raw_hashes", {}).get("vram_hash"),
        },
        "counts": {
            "base_filtered_frames": base["outcome"].get("frames_captured"),
            "overlay_filtered_frames": overlay["outcome"].get("frames_captured"),
            "base_cpu_to_vram_copy_count": base["outcome"].get("cpu_to_vram_copy_count"),
            "overlay_cpu_to_vram_copy_count": overlay["outcome"].get("cpu_to_vram_copy_count"),
        },
        "filtered_frames": filtered_cmp,
        "filtered_frame_black_stats": {
            "base": base_black,
            "overlay": overlay_black,
        },
        "filtered_visible_frames": visible_cmp,
        "cpu_to_vram_dumps": vram_cmp,
        "raw_hashes": {
            "base": base["outcome"].get("raw_hashes", {}),
            "overlay": overlay["outcome"].get("raw_hashes", {}),
        },
    }

    print(json.dumps(payload, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
