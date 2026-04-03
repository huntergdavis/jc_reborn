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


def aggregate_character(draws: list[dict]) -> dict:
    if not draws:
        raise ValueError("aggregate_character requires at least one draw")
    left = min(int(draw["x"]) for draw in draws)
    top = min(int(draw["y"]) for draw in draws)
    right = max(int(draw["x"]) + int(draw["width"]) for draw in draws)
    bottom = max(int(draw["y"]) + int(draw["height"]) for draw in draws)
    centers_x = [float(draw["x"]) + (float(draw["width"]) / 2.0) for draw in draws]
    centers_y = [float(draw["y"]) + (float(draw["height"]) / 2.0) for draw in draws]
    return {
        "character": draws[0]["entity"],
        "draw_count": len(draws),
        "bbox": {
            "left": left,
            "top": top,
            "right": right,
            "bottom": bottom,
            "width": right - left,
            "height": bottom - top,
        },
        "centroid": {
            "x": round(sum(centers_x) / len(centers_x), 3),
            "y": round(sum(centers_y) / len(centers_y), 3),
        },
        "sprite_sources": [
            {
                "bmp_name": draw.get("bmp_name"),
                "sprite_no": draw.get("sprite_no"),
                "x": draw.get("x"),
                "y": draw.get("y"),
                "width": draw.get("width"),
                "height": draw.get("height"),
            }
            for draw in sorted(
                draws,
                key=lambda draw: (
                    str(draw.get("bmp_name", "")),
                    int(draw.get("sprite_no", -1)),
                    int(draw.get("x", 0)),
                    int(draw.get("y", 0)),
                ),
            )
        ],
    }


def build_frame_truth(meta_path: Path) -> dict:
    summary = summarize(meta_path)
    by_character: dict[str, list[dict]] = {}
    for draw in summary.get("actor_candidates", []):
        entity = str(draw.get("entity") or "unknown")
        if entity in {"background_or_prop", "ambiguous", "unknown"}:
            continue
        by_character.setdefault(entity, []).append(draw)
    characters = [
        aggregate_character(draws)
        for _, draws in sorted(by_character.items())
    ]
    return {
        "frame_number": int(summary["frame_number"]),
        "frame_name": meta_path.stem,
        "scene_label": summary.get("scene_label"),
        "character_count": len(characters),
        "visible_characters": [item["character"] for item in characters],
        "characters": characters,
    }


def build_truth(root: Path) -> dict:
    scenes = []
    for scene_dir in sorted(path for path in root.iterdir() if path.is_dir()):
        meta_dir = scene_dir / "frame-meta"
        if not meta_dir.is_dir():
            continue
        frames = [build_frame_truth(meta_path) for meta_path in sorted(meta_dir.glob("frame_*.json"))]
        if not frames:
            continue
        scenes.append(
            {
                "scene_label": frames[0].get("scene_label") or scene_dir.name,
                "scene_dir": scene_dir.name,
                "frame_count": len(frames),
                "frames": frames,
            }
        )
    return {
        "root": str(root.resolve()),
        "scene_count": len(scenes),
        "scenes": scenes,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build screenshot-level character truth from captured frame-meta directories."
    )
    parser.add_argument("--root", type=Path, required=True, help="Capture root containing scene/frame-meta directories")
    parser.add_argument("--out-json", type=Path, required=True)
    args = parser.parse_args()

    truth = build_truth(args.root)
    args.out_json.write_text(json.dumps(truth, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
