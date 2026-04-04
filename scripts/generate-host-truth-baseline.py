#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path


def build_baseline(manifest: dict) -> dict:
    scenes = []
    for scene in manifest.get("scenes", []):
        frames = []
        for row in scene.get("rows", []):
            frames.append(
                {
                    "frame_number": int(row["frame_number"]),
                    "frame_name": row["frame_name"],
                    "expect_any_actor": bool(row.get("actor_summary")),
                    "expect_entities": sorted(row.get("actor_summary", {}).keys()),
                    "required_actor_bmps": list(row.get("bmp_names", [])),
                }
            )
        scenes.append(
            {
                "scene_label": scene["scene_label"],
                "source": "host_truth_manifest",
                "frame_expectations": frames,
            }
        )
    return {"scenes": scenes}


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate a zero-mismatch host-truth baseline from a host manifest")
    parser.add_argument("--manifest-json", type=Path, required=True)
    parser.add_argument("--out-json", type=Path, required=True)
    args = parser.parse_args()

    manifest = json.loads(args.manifest_json.read_text(encoding="utf-8"))
    baseline = build_baseline(manifest)
    args.out_json.write_text(json.dumps(baseline, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
