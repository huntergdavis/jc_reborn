#!/usr/bin/env python3
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path


def load_summarize():
    script_path = Path(__file__).with_name("summarize-frame-meta.py")
    spec = importlib.util.spec_from_file_location("summarize_frame_meta", script_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load summarizer from {script_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.summarize


summarize = load_summarize()


def scene_family(scene_label: str) -> str:
    upper = (scene_label or "").upper()
    if upper.startswith("FISHING"):
        return "fishing"
    if upper.startswith("MARY"):
        return "mary"
    return "unknown"


def classify_pose_labels(bmp_names: list[str], entities: list[str], family: str) -> list[str]:
    upper = [name.upper() for name in bmp_names]
    labels: list[str] = []
    if not entities:
        labels.append("no_actor_visible")
    else:
        labels.append("actor_visible")
    if "johnny" in entities:
        labels.append("johnny_visible")
    if "mary" in entities:
        labels.append("mary_visible")
    if any(name.startswith("MJFISH") or name.startswith("GJCATCH") for name in upper):
        labels.append("johnny_fishing_pose")
    if any(name == "JOHNWALK.BMP" for name in upper):
        labels.append("johnny_walking_pose")
    if any(name == "MJREAD.BMP" for name in upper):
        labels.append("johnny_reading_pose")
    if any(name.startswith("SMDATE") or name.startswith("SASKDATE") for name in upper):
        labels.append("date_scene_pose")
    if any(name.startswith("SBREAKUP") or name.startswith("SJBRAKUP") for name in upper):
        labels.append("breakup_scene_pose")
    if family == "fishing" and "johnny_visible" in labels and "johnny_fishing_pose" not in labels:
        labels.append("fishing_scene_actor_present")
    if family == "mary" and "johnny_visible" in labels and "mary_visible" not in labels:
        labels.append("mary_scene_johnny_only")
    return labels


def frame_region(summary: dict) -> dict | None:
    actors = summary.get("actor_candidates") or []
    if not actors:
        return None
    xs = [int(row["x"]) for row in actors]
    ys = [int(row["y"]) for row in actors]
    x2 = [int(row["x"]) + int(row["width"]) for row in actors]
    y2 = [int(row["y"]) + int(row["height"]) for row in actors]
    return {
        "x": min(xs),
        "y": min(ys),
        "width": max(x2) - min(xs),
        "height": max(y2) - min(ys),
    }


def compile_semantic_truth(root: Path) -> dict:
    scenes = []
    for scene_dir in sorted(path for path in root.iterdir() if path.is_dir()):
        meta_dir = scene_dir / "frame-meta"
        if not meta_dir.is_dir():
            continue
        rows = []
        label = scene_dir.name
        for meta_path in sorted(meta_dir.glob("frame_*.json")):
            summary = summarize(meta_path)
            label = summary.get("scene_label") or label
            entities = sorted(summary.get("actor_summary", {}).keys())
            bmp_names = [row.get("bmp_name") for row in summary.get("actor_candidates", [])]
            family = scene_family(label)
            rows.append(
                {
                    "frame_number": int(summary.get("frame_number", 0)),
                    "scene_label": label,
                    "scene_family": family,
                    "entities": entities,
                    "actor_count": int(summary.get("actor_candidate_draw_count", 0)),
                    "bmp_names": bmp_names,
                    "pose_labels": classify_pose_labels(bmp_names, entities, family),
                    "actor_region": frame_region(summary),
                }
            )
        scenes.append(
            {
                "scene_dir": scene_dir.name,
                "scene_label": label,
                "scene_family": scene_family(label),
                "rows": rows,
            }
        )
    return {"root": str(root.resolve()), "scenes": scenes}


def main() -> int:
    parser = argparse.ArgumentParser(description="Compile deterministic semantic truth from host frame metadata")
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--out-json", type=Path, required=True)
    args = parser.parse_args()

    payload = compile_semantic_truth(args.root)
    args.out_json.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
