#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Resolve the default capture start frame for a PS1 scene.")
    parser.add_argument("--scene", required=True, help='Scene spec, e.g. "STAND 2"')
    parser.add_argument(
        "--config",
        type=Path,
        default=Path("config/ps1/scene-capture-windows.json"),
        help="Scene capture window config JSON",
    )
    args = parser.parse_args()

    scene_key = args.scene.strip().replace(" ", "-").upper()
    payload = json.loads(args.config.read_text(encoding="utf-8"))
    default_start = int(payload.get("default", {}).get("start_frame", 1680))
    start_frame = int(payload.get("scenes", {}).get(scene_key, {}).get("start_frame", default_start))
    print(start_frame)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
