#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Resolve the default capture start frame for a PS1 scene.")
    parser.add_argument("--scene", required=True, help='Scene spec, e.g. "STAND 2"')
    parser.add_argument(
        "--field",
        default="start_frame",
        help="Config field to resolve (default: start_frame)",
    )
    parser.add_argument(
        "--default",
        default=None,
        help="Fallback value when the field is missing",
    )
    parser.add_argument(
        "--config",
        type=Path,
        default=Path("config/ps1/scene-capture-windows.json"),
        help="Scene capture window config JSON",
    )
    args = parser.parse_args()

    scene_key = args.scene.strip().replace(" ", "-").upper()
    payload = json.loads(args.config.read_text(encoding="utf-8"))
    default_section = payload.get("default", {})
    scene_section = payload.get("scenes", {}).get(scene_key, {})

    if args.default is not None:
        fallback = args.default
    elif args.field == "start_frame":
        fallback = default_section.get("start_frame", 1680)
    else:
        fallback = default_section.get(args.field)

    value = scene_section.get(args.field, fallback)
    if value is None:
        raise SystemExit(f"missing field '{args.field}' for scene {scene_key}")
    print(value)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
