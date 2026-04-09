#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def copy_tree(src: Path, dst: Path) -> None:
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)


def copy_file(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def main() -> int:
    parser = argparse.ArgumentParser(description="Copy a result bundle into a stable local directory and rewrite its published paths.")
    parser.add_argument("--result", type=Path, required=True, help="Source result.json or containing directory")
    parser.add_argument("--outdir", type=Path, required=True, help="Destination bundle directory")
    args = parser.parse_args()

    result_path = args.result.resolve()
    if result_path.is_dir():
        result_path = result_path / "result.json"
    payload = load_json(result_path)

    outdir = args.outdir.resolve()
    outdir.mkdir(parents=True, exist_ok=True)

    paths = payload.setdefault("paths", {})
    path_map = {
        "telemetry": outdir / "telemetry.json",
        "printf_log": outdir / "printf.log",
        "raw_hashes": outdir / "raw-hashes.json",
        "build_log": outdir / "build.log",
    }

    frames_dir_value = paths.get("frames_dir")
    if not frames_dir_value:
        raise SystemExit("result has no paths.frames_dir")
    frames_dir = Path(frames_dir_value)
    if not frames_dir.is_absolute():
        frames_dir = (result_path.parent / frames_dir).resolve()
    if not frames_dir.is_dir():
        raise SystemExit(f"frames dir not found: {frames_dir}")

    copy_tree(frames_dir, outdir / "filtered-frames")
    paths["frames_dir"] = str(outdir / "filtered-frames")
    paths["output_dir"] = str(outdir)

    for key, dst in path_map.items():
        src_value = paths.get(key)
        if not src_value:
            continue
        src = Path(src_value)
        if not src.is_absolute():
            src = (result_path.parent / src).resolve()
        if not src.is_file():
            continue
        copy_file(src, dst)
        paths[key] = str(dst)

    (outdir / "result.json").write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(outdir / "result.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
