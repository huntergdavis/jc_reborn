#!/usr/bin/env python3
"""
Build a JSON index of all captured reference frames.

Reads regtest-references/ and produces regtest-references/index.json with a
structured catalog of every scene and special screen that has been captured.

Usage:
    ./scripts/build-reference-index.py
    ./scripts/build-reference-index.py --refdir regtest-references/
    ./scripts/build-reference-index.py --pretty
"""

import argparse
import json
import os
import sys
from datetime import datetime, timezone
from pathlib import Path


def read_json_file(path: Path) -> dict | None:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return None


def find_project_root() -> Path:
    """Walk up from script location to find the project root."""
    here = Path(__file__).resolve().parent
    # Script is in scripts/, project root is one level up.
    root = here.parent
    if (root / "config" / "ps1" / "regtest-scenes.txt").is_file():
        return root
    # Fallback: cwd
    return Path.cwd()


def load_scene_list(project_root: Path) -> dict:
    """Parse regtest-scenes.txt into a lookup dict keyed by 'ADS-TAG'."""
    scene_file = project_root / "config" / "ps1" / "regtest-scenes.txt"
    scenes = {}
    if not scene_file.is_file():
        return scenes

    for line in scene_file.read_text(encoding="utf-8").splitlines():
        line = line.split("#")[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) < 4:
            continue
        ads_name, tag, scene_index, status = parts[0], parts[1], parts[2], parts[3]
        boot_string = " ".join(parts[4:]) if len(parts) > 4 else ""
        key = f"{ads_name}-{tag}"
        scenes[key] = {
            "ads_name": ads_name,
            "ads_file": f"{ads_name}.ADS",
            "tag": int(tag),
            "scene_index": int(scene_index) if scene_index.isdigit() else None,
            "status": status,
            "boot_string": boot_string,
        }
    return scenes


def scan_scene_dir(scene_dir: Path) -> dict | None:
    """Read a scene reference directory and return its index entry."""
    metadata_file = scene_dir / "metadata.json"
    result_file = scene_dir / "result.json"

    # List frame PNGs
    frame_files = sorted(f.name for f in scene_dir.glob("**/frame_*.png"))
    if not frame_files and not metadata_file.is_file():
        return None

    entry = {
        "frames": frame_files,
        "frame_count": len(frame_files),
        "path": str(scene_dir.name),
    }

    # Merge metadata if available.
    meta = read_json_file(metadata_file) if metadata_file.is_file() else None
    if meta:
        for key in (
            "ads_name",
            "ads_file",
            "tag",
            "scene_index",
            "status",
            "boot_string",
            "capture_date",
            "capture_frames",
            "capture_start_frame",
            "capture_interval",
            "regtest_exit_code",
        ):
            if key in meta:
                entry[key] = meta[key]

    # Carry the normalized published result metadata too, so downstream tools can
    # tell whether a reference bundle reflects a reviewed late-window capture.
    result = read_json_file(result_file) if result_file.is_file() else None
    if result:
        config = result.get("config") or {}
        scene = result.get("scene") or {}
        if "start_frame" in config:
            entry["start_frame"] = config["start_frame"]
        if "frames" in config and "capture_frames" not in entry:
            entry["capture_frames"] = config["frames"]
        if "capture_start_frame" in scene and "capture_start_frame" not in entry:
            entry["capture_start_frame"] = scene["capture_start_frame"]
        if "frames_dir_layout" in config:
            entry["frames_dir_layout"] = config["frames_dir_layout"]

    return entry


def scan_special_dir(special_dir: Path) -> dict | None:
    """Read a special screen directory (title, transitions)."""
    frame_files = sorted(f.name for f in special_dir.glob("**/frame_*.png"))
    if not frame_files:
        return None

    entry = {
        "frames": frame_files,
        "frame_count": len(frame_files),
        "path": str(special_dir.name),
    }

    metadata_file = special_dir / "metadata.json"
    if metadata_file.is_file():
        try:
            meta = json.loads(metadata_file.read_text(encoding="utf-8"))
            for key in (
                "screen_type", "description", "boot_string",
                "capture_date", "capture_frames", "capture_interval",
                "regtest_exit_code", "notes",
            ):
                if key in meta:
                    entry[key] = meta[key]
        except (json.JSONDecodeError, OSError):
            pass

    return entry


def build_index(ref_dir: Path, project_root: Path) -> dict:
    """Build the complete reference index."""
    scene_lookup = load_scene_list(project_root)

    index = {
        "version": 1,
        "generated": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "reference_dir": str(ref_dir),
        "scenes": {},
        "special_screens": {},
        "stats": {
            "total_scenes": 0,
            "total_frames": 0,
            "verified_scenes": 0,
            "special_screens": 0,
        },
    }

    # Known special screen directory names
    special_names = {"title", "transitions", "ocean", "black"}

    # Scan all subdirectories
    if not ref_dir.is_dir():
        return index

    for subdir in sorted(ref_dir.iterdir()):
        if not subdir.is_dir():
            continue
        dir_name = subdir.name

        if dir_name in special_names:
            # Special screen
            entry = scan_special_dir(subdir)
            if entry:
                index["special_screens"][dir_name] = entry
                index["stats"]["special_screens"] += 1
                index["stats"]["total_frames"] += entry["frame_count"]
        elif dir_name.startswith("."):
            # Skip hidden directories
            continue
        else:
            # Scene directory — expected format: ADS_NAME-TAG
            entry = scan_scene_dir(subdir)
            if entry is None:
                continue

            # Enrich with scene list data if available
            if dir_name in scene_lookup:
                scene_info = scene_lookup[dir_name]
                for key, val in scene_info.items():
                    if key not in entry:
                        entry[key] = val

            # Fill in ads/tag from directory name if not in metadata
            if "ads_name" not in entry:
                parts = dir_name.rsplit("-", 1)
                if len(parts) == 2:
                    entry["ads_name"] = parts[0].upper()
                    try:
                        entry["tag"] = int(parts[1])
                    except ValueError:
                        entry["tag"] = parts[1]

            index["scenes"][dir_name] = entry
            index["stats"]["total_scenes"] += 1
            index["stats"]["total_frames"] += entry["frame_count"]

            if entry.get("status") == "verified":
                index["stats"]["verified_scenes"] += 1

    return index


def main():
    parser = argparse.ArgumentParser(
        description="Build a JSON index of all captured reference frames."
    )
    parser.add_argument(
        "--refdir",
        default=None,
        help="Path to regtest-references/ directory (default: auto-detect)",
    )
    parser.add_argument(
        "--pretty",
        action="store_true",
        help="Pretty-print the JSON output",
    )
    parser.add_argument(
        "--stdout",
        action="store_true",
        help="Print to stdout instead of writing index.json",
    )
    args = parser.parse_args()

    project_root = find_project_root()

    if args.refdir:
        ref_dir = Path(args.refdir).resolve()
    else:
        ref_dir = project_root / "regtest-references"

    if not ref_dir.is_dir():
        print(f"ERROR: Reference directory not found: {ref_dir}", file=sys.stderr)
        print("Run ./scripts/capture-reference-frames.sh first.", file=sys.stderr)
        sys.exit(1)

    index = build_index(ref_dir, project_root)

    indent = 2 if args.pretty else None
    output = json.dumps(index, indent=indent, ensure_ascii=False)

    if args.stdout:
        print(output)
    else:
        index_file = ref_dir / "index.json"
        index_file.write_text(output + "\n", encoding="utf-8")
        print(f"Reference index written to {index_file}", file=sys.stderr)
        print(f"  Scenes:          {index['stats']['total_scenes']}", file=sys.stderr)
        print(f"  Verified:        {index['stats']['verified_scenes']}", file=sys.stderr)
        print(f"  Special screens: {index['stats']['special_screens']}", file=sys.stderr)
        print(f"  Total frames:    {index['stats']['total_frames']}", file=sys.stderr)


if __name__ == "__main__":
    main()
