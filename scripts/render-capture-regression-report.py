#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def summarize(report: dict, label_key: str) -> list[dict]:
    rows = []
    for scene in report.get("scenes", []):
        rows.append(
            {
                label_key: scene.get(label_key),
                "passed": scene.get("passed", False),
                "failure_count": scene.get("failure_count", 0),
            }
        )
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description="Render a consolidated capture regression report.")
    ap.add_argument("--frame-image", required=True)
    ap.add_argument("--frame-meta", required=True)
    ap.add_argument("--semantic", required=True)
    ap.add_argument("--out-json", required=True)
    args = ap.parse_args()

    frame_image = load(Path(args.frame_image))
    frame_meta = load(Path(args.frame_meta))
    semantic = load(Path(args.semantic))

    payload = {
        "passed": all(
            report.get("passed", False)
            for report in (frame_image, frame_meta, semantic)
        ),
        "checks": {
            "frame-image": {
                "passed": frame_image.get("passed", False),
                "failure_count": frame_image.get("failure_count", 0),
                "scenes": summarize(frame_image, "scene"),
            },
            "frame-meta": {
                "passed": frame_meta.get("passed", False),
                "failure_count": frame_meta.get("failure_count", 0),
                "scenes": summarize(frame_meta, "scene"),
            },
            "semantic": {
                "passed": semantic.get("passed", False),
                "failure_count": semantic.get("failure_count", 0),
                "scenes": summarize(semantic, "scene_label"),
            },
        },
    }
    Path(args.out_json).write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
