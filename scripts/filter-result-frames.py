#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser(description="Materialize a filtered result.json with a late frame window.")
    parser.add_argument("--result", type=Path, required=True, help="Source result.json or containing directory")
    parser.add_argument("--outdir", type=Path, required=True, help="Output directory for filtered result wrapper")
    parser.add_argument("--start-frame", type=int, default=None, help="First frame number to keep")
    args = parser.parse_args()

    result_path = args.result.resolve()
    if result_path.is_dir():
        result_path = result_path / "result.json"
    payload = load_json(result_path)
    outdir = args.outdir.resolve()
    outdir.mkdir(parents=True, exist_ok=True)

    if args.start_frame is None:
        grace = 1800
        tolerance = 120
        start_frame = max(0, grace - tolerance)
    else:
        start_frame = max(0, int(args.start_frame))

    frames_dir_value = (payload.get("paths") or {}).get("frames_dir")
    if not frames_dir_value:
        raise SystemExit("result has no paths.frames_dir")
    frames_dir = Path(frames_dir_value)
    if not frames_dir.is_absolute():
        frames_dir = (result_path.parent / frames_dir).resolve()
    if not frames_dir.is_dir():
        raise SystemExit(f"frames dir not found: {frames_dir}")

    filtered_dir = outdir / "filtered-frames"
    filtered_dir.mkdir(parents=True, exist_ok=True)
    kept = 0
    for frame_path in sorted(frames_dir.glob("frame_*.png")):
        stem = frame_path.stem
        try:
            frame_no = int(stem.split("_", 1)[1])
        except Exception:
            continue
        if frame_no < start_frame:
            continue
        shutil.copy2(frame_path, filtered_dir / frame_path.name)
        kept += 1

    output = json.loads(json.dumps(payload))
    output.setdefault("config", {})
    output["config"]["start_frame"] = start_frame
    output["config"]["frames_dir_layout"] = "flat"
    output.setdefault("paths", {})
    output["paths"]["output_dir"] = str(outdir)
    output["paths"]["frames_dir"] = str(filtered_dir)
    if "outcome" in output:
        output["outcome"]["frames_captured"] = kept

    (outdir / "result.json").write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(outdir / "result.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
