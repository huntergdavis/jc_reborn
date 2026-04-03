#!/usr/bin/env python3
from __future__ import annotations

import argparse
import importlib.util
import json
import math
from pathlib import Path


def load_truth_builder():
    script_path = Path(__file__).with_name("build-character-truth.py")
    spec = importlib.util.spec_from_file_location("build_character_truth", script_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load truth builder from {script_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.build_truth


build_truth = load_truth_builder()


def load_truth(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def scene_map(truth: dict) -> dict[str, dict]:
    return {scene["scene_label"]: scene for scene in truth.get("scenes", [])}


def frame_map(scene: dict) -> dict[int, dict]:
    return {int(frame["frame_number"]): frame for frame in scene.get("frames", [])}


def char_map(frame: dict) -> dict[str, dict]:
    return {item["character"]: item for item in frame.get("characters", [])}


def compare_character(expected: dict, actual: dict, position_tolerance: float) -> dict:
    expected_center = expected["centroid"]
    actual_center = actual["centroid"]
    dx = round(float(actual_center["x"]) - float(expected_center["x"]), 3)
    dy = round(float(actual_center["y"]) - float(expected_center["y"]), 3)
    distance = round(math.hypot(dx, dy), 3)
    bbox_expected = expected["bbox"]
    bbox_actual = actual["bbox"]
    bbox_delta = {
        "left": int(bbox_actual["left"]) - int(bbox_expected["left"]),
        "top": int(bbox_actual["top"]) - int(bbox_expected["top"]),
        "right": int(bbox_actual["right"]) - int(bbox_expected["right"]),
        "bottom": int(bbox_actual["bottom"]) - int(bbox_expected["bottom"]),
        "width": int(bbox_actual["width"]) - int(bbox_expected["width"]),
        "height": int(bbox_actual["height"]) - int(bbox_expected["height"]),
    }
    problems = []
    if int(actual["draw_count"]) != int(expected["draw_count"]):
        problems.append("draw_count_mismatch")
    if distance > position_tolerance:
        problems.append("position_drift")
    if any(abs(value) > position_tolerance for value in bbox_delta.values()):
        problems.append("bbox_drift")
    return {
        "character": expected["character"],
        "expected": expected,
        "actual": actual,
        "centroid_delta": {"dx": dx, "dy": dy, "distance": distance},
        "bbox_delta": bbox_delta,
        "status": "ok" if not problems else "mismatch",
        "problems": problems,
    }


def compare(expected_truth: dict, actual_truth: dict, position_tolerance: float) -> dict:
    rows = []
    mismatch_count = 0
    expected_scenes = scene_map(expected_truth)
    actual_scenes = scene_map(actual_truth)
    for scene_label, expected_scene in sorted(expected_scenes.items()):
        actual_scene = actual_scenes.get(scene_label)
        if actual_scene is None:
            rows.append(
                {
                    "scene_label": scene_label,
                    "frame_number": None,
                    "status": "missing_scene",
                    "problems": ["scene_missing"],
                }
            )
            mismatch_count += 1
            continue
        actual_frames = frame_map(actual_scene)
        for frame_number, expected_frame in sorted(frame_map(expected_scene).items()):
            actual_frame = actual_frames.get(frame_number)
            if actual_frame is None:
                rows.append(
                    {
                        "scene_label": scene_label,
                        "frame_number": frame_number,
                        "frame_name": expected_frame.get("frame_name"),
                        "status": "missing_frame",
                        "problems": ["frame_missing"],
                    }
                )
                mismatch_count += 1
                continue
            expected_chars = char_map(expected_frame)
            actual_chars = char_map(actual_frame)
            missing = sorted(set(expected_chars) - set(actual_chars))
            unexpected = sorted(set(actual_chars) - set(expected_chars))
            shared = [
                compare_character(expected_chars[name], actual_chars[name], position_tolerance)
                for name in sorted(set(expected_chars) & set(actual_chars))
            ]
            problems = []
            if missing:
                problems.append("missing_characters")
            if unexpected:
                problems.append("unexpected_characters")
            if any(item["status"] != "ok" for item in shared):
                problems.append("position_or_shape_drift")
            status = "ok" if not problems else "mismatch"
            if status != "ok":
                mismatch_count += 1
            rows.append(
                {
                    "scene_label": scene_label,
                    "frame_number": frame_number,
                    "frame_name": expected_frame.get("frame_name"),
                    "status": status,
                    "problems": problems,
                    "missing_characters": missing,
                    "unexpected_characters": unexpected,
                    "shared_characters": shared,
                }
            )
    return {
        "expected_root": expected_truth.get("root"),
        "actual_root": actual_truth.get("root"),
        "position_tolerance": position_tolerance,
        "mismatch_count": mismatch_count,
        "rows": rows,
        "mismatches": [row for row in rows if row.get("status") != "ok"],
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compare screenshot-level character truth against a new capture root or truth JSON."
    )
    parser.add_argument("--expected-truth-json", type=Path, required=True)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--actual-truth-json", type=Path)
    group.add_argument("--actual-root", type=Path)
    parser.add_argument("--position-tolerance", type=float, default=12.0)
    parser.add_argument("--out-json", type=Path, required=True)
    args = parser.parse_args()

    expected_truth = load_truth(args.expected_truth_json)
    actual_truth = load_truth(args.actual_truth_json) if args.actual_truth_json else build_truth(args.actual_root)
    report = compare(expected_truth, actual_truth, args.position_tolerance)
    args.out_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
