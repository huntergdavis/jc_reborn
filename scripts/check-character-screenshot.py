#!/usr/bin/env python3
from __future__ import annotations

import argparse
import importlib.util
import json
import re
import shutil
import sys
from pathlib import Path


def load_module(script_name: str, module_name: str):
    script_path = Path(__file__).with_name(script_name)
    spec = importlib.util.spec_from_file_location(module_name, script_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load module from {script_path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module


build_character_truth = load_module("build-character-truth.py", "build_character_truth")
compare_character_truth = load_module("compare-character-truth.py", "compare_character_truth")
decode_capture_overlay = load_module("decode-capture-overlay.py", "decode_capture_overlay")
decode_ps1_bars = load_module("decode-ps1-bars.py", "decode_ps1_bars")
render_character_truth_report = load_module("render-character-truth-report.py", "render_character_truth_report")
summarize_frame_meta = load_module("summarize-frame-meta.py", "summarize_frame_meta")


def bmp_name_hash(name: str) -> int:
    value = 0
    for ch in (name or ""):
        value = ((value << 5) - value + ord(ch)) & 0xFFFFFFFF
    return value & 0xFFFFFF


def scene_slug(scene_label: str) -> str:
    return "".join(ch for ch in scene_label.lower() if ch.isalnum())


def infer_frame_number_from_name(image_path: Path) -> int:
    match = re.search(r"(\d+)", image_path.stem)
    if not match:
        raise ValueError("could not infer frame number from image name")
    return int(match.group(1))


def load_decoded_overlay(image_path: Path) -> dict:
    sidecar_path = image_path.with_name(f"{image_path.stem}.overlay.json")
    if sidecar_path.is_file():
        return json.loads(sidecar_path.read_text(encoding="utf-8"))

    from PIL import Image
    with Image.open(image_path) as image:
        return decode_capture_overlay.decode_image(image)


def build_hash_lookup(lookup_root: Path) -> tuple[dict[int, str], dict[int, list[str]]]:
    names_by_hash: dict[int, set[str]] = {}
    for json_path in sorted(lookup_root.rglob("*.json")):
        try:
            payload = json.loads(json_path.read_text(encoding="utf-8"))
        except Exception:
            continue
        if not isinstance(payload, dict):
            continue
        draws = payload.get("visible_draws") or payload.get("draws") or []
        if not isinstance(draws, list):
            continue
        for draw in draws:
            if not isinstance(draw, dict):
                continue
            bmp_name = draw.get("bmp_name")
            if not bmp_name:
                continue
            names_by_hash.setdefault(bmp_name_hash(str(bmp_name)), set()).add(str(bmp_name))
    resolved: dict[int, str] = {}
    collisions: dict[int, list[str]] = {}
    for hash_value, names in names_by_hash.items():
        sorted_names = sorted(names)
        if len(sorted_names) == 1:
            resolved[hash_value] = sorted_names[0]
        else:
            collisions[hash_value] = sorted_names
    return resolved, collisions


def build_actor_candidates(decoded: dict, hash_lookup: dict[int, str]) -> tuple[list[dict], list[dict]]:
    resolved_draws: list[dict] = []
    unresolved_draws: list[dict] = []
    for draw in decoded.get("draws", []):
        hash_value = int(draw.get("bmp_name_hash", -1))
        bmp_name = hash_lookup.get(hash_value)
        row = {
            "bmp_name": bmp_name,
            "sprite_no": int(draw.get("sprite_no", -1)),
            "image_no": int(draw.get("image_no", -1)),
            "x": int(draw.get("x", 0)),
            "y": int(draw.get("y", 0)),
            "width": int(draw.get("width", 0)),
            "height": int(draw.get("height", 0)),
            "flipped": bool(draw.get("flipped", False)),
            "bmp_name_hash": hash_value,
        }
        if bmp_name is None:
            unresolved_draws.append(row)
            continue
        row["entity"] = summarize_frame_meta.classify_entity(bmp_name)
        row["actor_candidate"] = summarize_frame_meta.is_actor_candidate(bmp_name)
        resolved_draws.append(row)
    actor_candidates = [row for row in resolved_draws if row.get("actor_candidate")]
    return actor_candidates, unresolved_draws


def build_single_frame_truth(
    image_path: Path,
    scene_label: str,
    frame_number: int,
    actor_candidates: list[dict],
    actual_root: Path,
    ) -> dict:
    frame_name = f"frame_{frame_number:05d}"
    return {
        "root": str(actual_root.resolve()),
        "scene_count": 1,
        "scenes": [
            {
                "scene_label": scene_label,
                "scene_dir": scene_slug(scene_label),
                "frame_count": 1,
                "frames": [
                    build_character_truth.build_frame_truth_from_actor_candidates(
                        actor_candidates,
                        frame_number,
                        frame_name,
                        scene_label,
                    )
                ],
            }
        ],
    }


def build_single_frame_truth_from_characters(
    scene_label: str,
    frame_number: int,
    image_suffix: str,
    characters: list[dict],
    actual_root: Path,
) -> dict:
    frame_name = f"frame_{frame_number:05d}{image_suffix.lower()}"
    return {
        "root": str(actual_root.resolve()),
        "scene_count": 1,
        "scenes": [
            {
                "scene_label": scene_label,
                "scene_dir": scene_slug(scene_label),
                "frame_count": 1,
                "frames": [
                    {
                        "frame_number": int(frame_number),
                        "frame_name": frame_name,
                        "scene_label": scene_label,
                        "character_count": len(characters),
                        "visible_characters": [item["character"] for item in characters],
                        "characters": characters,
                    }
                ],
            }
        ],
    }


def filter_expected_truth(expected_truth: dict, scene_label: str, frame_number: int) -> dict:
    for scene in expected_truth.get("scenes", []):
        if scene.get("scene_label") != scene_label:
            continue
        for frame in scene.get("frames", []):
            if int(frame.get("frame_number", -1)) != frame_number:
                continue
            return {
                "root": expected_truth.get("root"),
                "scene_count": 1,
                "scenes": [
                    {
                        "scene_label": scene_label,
                        "scene_dir": scene.get("scene_dir", scene_slug(scene_label)),
                        "frame_count": 1,
                        "frames": [frame],
                    }
                ],
            }
    raise SystemExit(f"expected truth missing scene={scene_label!r} frame={frame_number}")


def infer_scene_label(expected_truth: dict) -> str:
    scenes = expected_truth.get("scenes", [])
    if len(scenes) != 1:
        raise SystemExit("pass --scene-label when expected truth contains more than one scene")
    scene_label = scenes[0].get("scene_label")
    if not scene_label:
        raise SystemExit("could not infer scene label from expected truth; pass --scene-label")
    return str(scene_label)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Decode an overlay screenshot into character truth, compare it against expected truth, and render a small HTML report."
    )
    parser.add_argument("--image", type=Path, required=True, help="Overlay-bearing host or DuckStation screenshot")
    parser.add_argument("--expected-truth-json", type=Path)
    parser.add_argument(
        "--expected-root",
        type=Path,
        help="Capture/reference root containing scene/frame-meta directories; expected truth will be built automatically",
    )
    parser.add_argument("--scene-label", help="Defaults to the only scene in expected truth when unambiguous")
    parser.add_argument("--frame-number", type=int, help="Expected frame number; defaults to digits from image name")
    parser.add_argument("--lookup-root", type=Path, default=Path("host-script-review"), help="Root used to resolve overlay bmp_name_hash values")
    parser.add_argument("--position-tolerance", type=float, default=12.0)
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()

    if bool(args.expected_truth_json) == bool(args.expected_root):
        raise SystemExit("pass exactly one of --expected-truth-json or --expected-root")

    if args.expected_truth_json is not None:
        expected_truth = json.loads(args.expected_truth_json.read_text(encoding="utf-8"))
    else:
        expected_truth = build_character_truth.build_truth(args.expected_root)

    scene_label = args.scene_label if args.scene_label is not None else infer_scene_label(expected_truth)
    hash_lookup, collisions = build_hash_lookup(args.lookup_root)

    with args.image.open("rb"):
        pass
    decoded = load_decoded_overlay(args.image)
    if args.frame_number is not None:
        frame_number = args.frame_number
    elif decoded.get("frame_number") is not None:
        frame_number = int(decoded["frame_number"])
    else:
        frame_number = infer_frame_number_from_name(args.image)

    actor_candidates, unresolved_draws = build_actor_candidates(decoded, hash_lookup)

    actual_root = args.out_dir / "actual-root"
    frame_name = f"frame_{frame_number:05d}{args.image.suffix.lower()}"
    frame_dir = actual_root / scene_slug(scene_label) / "frames"
    frame_dir.mkdir(parents=True, exist_ok=True)
    frame_copy = frame_dir / frame_name
    shutil.copy2(args.image, frame_copy)

    decode_method = "capture-overlay"
    actor_panel = None
    if actor_candidates:
        actual_truth = build_single_frame_truth(
            args.image,
            scene_label,
            frame_number,
            actor_candidates,
            actual_root,
        )
    else:
        actor_panel = decode_ps1_bars.decode_actor_panel(args.image)
        if actor_panel.get("character_count", 0) > 0:
            decode_method = "ps1-actor-bars"
            actual_truth = build_single_frame_truth_from_characters(
                scene_label,
                frame_number,
                args.image.suffix,
                actor_panel.get("characters", []),
                actual_root,
            )
        else:
            actual_truth = build_single_frame_truth(
                args.image,
                scene_label,
                frame_number,
                actor_candidates,
                actual_root,
            )
    expected_subset = filter_expected_truth(expected_truth, scene_label, frame_number)
    report = compare_character_truth.compare(expected_subset, actual_truth, args.position_tolerance)
    report["overlay_debug"] = {
        "decode_method": decode_method,
        "draw_count": int(decoded.get("draw_count", 0)),
        "embedded_draw_count": int(decoded.get("embedded_draw_count", 0)),
        "resolved_draw_count": len(actor_candidates),
        "unresolved_draw_count": len(unresolved_draws),
        "hash_lookup_size": len(hash_lookup),
        "hash_collision_count": len(collisions),
        "unresolved_draws": unresolved_draws,
        "actor_panel": actor_panel,
    }

    args.out_dir.mkdir(parents=True, exist_ok=True)
    actual_truth_json = args.out_dir / "actual-character-truth.json"
    expected_subset_json = args.out_dir / "expected-character-truth.json"
    report_json = args.out_dir / "character-truth-report.json"
    report_html = args.out_dir / "character-truth-report.html"
    actual_truth_json.write_text(json.dumps(actual_truth, indent=2) + "\n", encoding="utf-8")
    expected_subset_json.write_text(json.dumps(expected_subset, indent=2) + "\n", encoding="utf-8")
    report_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    report_html.write_text(
        render_character_truth_report.build_html(
            report,
            report_html,
            f"Character Screenshot Check: {scene_label} frame {frame_number}",
        ),
        encoding="utf-8",
    )
    print(json.dumps(
        {
            "actual_truth_json": str(actual_truth_json.resolve()),
            "expected_truth_json": str(expected_subset_json.resolve()),
            "report_json": str(report_json.resolve()),
            "report_html": str(report_html.resolve()),
            "mismatch_count": int(report.get("mismatch_count", 0)),
            "unresolved_draw_count": len(unresolved_draws),
        },
        indent=2,
    ))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
