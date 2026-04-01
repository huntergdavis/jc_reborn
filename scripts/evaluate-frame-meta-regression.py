#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def summarize_frame(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    return {
        "frame": path.stem,
        "draw_count": data.get("draw_count"),
        "visible_draw_count": data.get("visible_draw_count"),
        "draw_bmps": [d.get("bmp_name") for d in data.get("draws", [])],
        "visible_bmps": [d.get("bmp_name") for d in data.get("visible_draws", [])[:12]],
        "visible_surface_roles": [d.get("surface_role") for d in data.get("visible_draws", [])[:12]],
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Compare canonical frame-meta outputs against a published baseline.")
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--out-json", required=True)
    args = ap.parse_args()

    baseline = json.loads(Path(args.baseline).read_text(encoding="utf-8"))
    root = Path(args.root)
    failures = []
    scenes = []
    for scene_name, expected_rows in baseline.get("scenes", {}).items():
        scene_failures = []
        scene_dir = root / scene_name / "frame-meta"
        for expected in expected_rows:
            frame_name = expected["frame"]
            path = scene_dir / f"{frame_name}.json"
            if not path.is_file():
                scene_failures.append({"scene": scene_name, "frame": frame_name, "field": "missing_frame_meta", "expected": True, "actual": False})
                continue
            current = summarize_frame(path)
            for key in ("draw_count", "visible_draw_count", "draw_bmps", "visible_bmps", "visible_surface_roles"):
                if current.get(key) != expected.get(key):
                    scene_failures.append(
                        {
                            "scene": scene_name,
                            "frame": frame_name,
                            "field": key,
                            "expected": expected.get(key),
                            "actual": current.get(key),
                        }
                    )
        failures.extend(scene_failures)
        scenes.append({"scene": scene_name, "passed": not scene_failures, "failure_count": len(scene_failures), "failures": scene_failures})

    payload = {
        "passed": not failures,
        "scene_count": len(scenes),
        "failure_count": len(failures),
        "scenes": scenes,
        "failures": failures,
    }
    Path(args.out_json).write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
