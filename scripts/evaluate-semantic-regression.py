#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def load_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def compare_scene(label: str, expected: dict, current: dict) -> list[dict]:
    failures = []
    summary = current.get("scene_summary", {})
    rows = {row["frame_number"]: row for row in current.get("rows", [])}

    for key in (
        "scene_signature",
        "timeline_signature",
        "dominant_frame_state",
        "dominant_activity",
        "transition_points",
    ):
        expected_value = expected.get(key)
        current_value = summary.get(key)
        if current_value != expected_value:
            failures.append(
                {
                    "scene_label": label,
                    "scope": "scene",
                    "field": key,
                    "expected": expected_value,
                    "actual": current_value,
                }
            )

    for expected_row in expected.get("frames", []):
        frame_number = expected_row["frame_number"]
        row = rows.get(frame_number)
        if row is None:
            failures.append(
                {
                    "scene_label": label,
                    "scope": "frame",
                    "frame_number": frame_number,
                    "field": "missing_frame",
                    "expected": expected_row,
                    "actual": None,
                }
            )
            continue
        for key in ("frame_state", "primary_subject", "primary_activity", "frame_signature"):
            expected_value = expected_row.get(key)
            current_value = row.get(key)
            if current_value != expected_value:
                failures.append(
                    {
                        "scene_label": label,
                        "scope": "frame",
                        "frame_number": frame_number,
                        "field": key,
                        "expected": expected_value,
                        "actual": current_value,
                    }
                )

    return failures


def main() -> int:
    ap = argparse.ArgumentParser(description="Compare semantic-truth against a semantic regression baseline.")
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--semantic-truth", required=True)
    ap.add_argument("--out-json", required=True)
    args = ap.parse_args()

    baseline = load_json(Path(args.baseline))
    semantic_truth = load_json(Path(args.semantic_truth))
    current_scenes = {scene["scene_label"]: scene for scene in semantic_truth.get("scenes", [])}

    failures = []
    per_scene = []
    for label, expected in baseline.get("scenes", {}).items():
        scene = current_scenes.get(label)
        if scene is None:
            scene_failures = [
                {
                    "scene_label": label,
                    "scope": "scene",
                    "field": "missing_scene",
                    "expected": True,
                    "actual": False,
                }
            ]
        else:
            scene_failures = compare_scene(label, expected, scene)
        failures.extend(scene_failures)
        per_scene.append(
            {
                "scene_label": label,
                "passed": not scene_failures,
                "failure_count": len(scene_failures),
                "failures": scene_failures,
            }
        )

    payload = {
        "passed": not failures,
        "scene_count": len(per_scene),
        "failure_count": len(failures),
        "scenes": per_scene,
        "failures": failures,
    }
    Path(args.out_json).write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
