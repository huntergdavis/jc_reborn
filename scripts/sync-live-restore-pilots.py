#!/usr/bin/env python3
"""Generate ps1_restore_pilots.h from verified exact-scene regtest entries."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


DEFAULT_SCENE_LIST = Path("config/ps1/regtest-scenes.txt")
DEFAULT_SPEC_DIR = Path("docs/ps1/research/generated/restore_scene_specs_full_2026-03-21")
DEFAULT_HEADER = Path("ps1_restore_pilots.h")
GENERATOR = Path("scripts/generate-restore-pilots-header.py")
EXTRA_SPEC_PATHS = [
    Path("docs/ps1/research/generated/restore_scene_specs_full_2026-03-21/fishing-ads-tag-01.json"),
]


def parse_scene_list(path: Path) -> list[dict]:
    verified: list[dict] = []
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(maxsplit=5)
        if len(parts) < 5:
            continue
        ads_name = parts[0]
        ads_tag = int(parts[1])
        scene_index = int(parts[2])
        status = parts[3]
        boot = parts[4] if len(parts) == 5 else parts[4] + " " + parts[5]
        if status != "verified":
            continue
        verified.append(
            {
                "ads_name": f"{ads_name}.ADS",
                "ads_tag": ads_tag,
                "scene_index": scene_index,
                "boot": boot,
            }
        )
    verified.sort(key=lambda row: (row["ads_name"], row["scene_index"], row["ads_tag"]))
    return verified


def collect_specs(spec_dir: Path, verified: list[dict]) -> list[Path]:
    by_scene_index: dict[int, Path] = {}
    for path in sorted(spec_dir.glob("*.json")):
        if path.name == "summary.json":
            continue
        data = json.loads(path.read_text(encoding="utf-8"))
        by_scene_index[int(data["selected_scene"]["scene_index"])] = path

    missing = [row for row in verified if row["scene_index"] not in by_scene_index]
    if missing:
        missing_desc = ", ".join(
            f"{row['ads_name']} tag {row['ads_tag']} scene {row['scene_index']}" for row in missing
        )
        raise SystemExit(f"Missing restore scene specs for verified scenes: {missing_desc}")

    spec_paths = [by_scene_index[row["scene_index"]] for row in verified]

    for extra_path in EXTRA_SPEC_PATHS:
        if not extra_path.exists():
            raise SystemExit(f"Missing extra restore spec: {extra_path}")
        if extra_path not in spec_paths:
            spec_paths.append(extra_path)

    return spec_paths


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--scene-list", type=Path, default=DEFAULT_SCENE_LIST)
    ap.add_argument("--spec-dir", type=Path, default=DEFAULT_SPEC_DIR)
    ap.add_argument("--header", type=Path, default=DEFAULT_HEADER)
    args = ap.parse_args()

    verified = parse_scene_list(args.scene_list)
    if not verified:
        raise SystemExit("No verified scenes found in regtest scene list.")

    specs = collect_specs(args.spec_dir, verified)

    cmd = ["python3", str(GENERATOR), "--header", str(args.header)]
    for spec in specs:
        cmd.extend(["--spec", str(spec)])
    subprocess.run(cmd, check=True)

    print(
        json.dumps(
            {
                "scene_list": str(args.scene_list),
                "spec_dir": str(args.spec_dir),
                "header": str(args.header),
                "verified_scene_count": len(verified),
                "spec_count": len(specs),
                "verified_scenes": verified,
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
