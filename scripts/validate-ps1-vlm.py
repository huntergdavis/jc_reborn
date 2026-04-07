#!/usr/bin/env python3
"""
Validate PS1 scene output with the local VLM and reference-bank hints.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
from collections import Counter
from pathlib import Path
from typing import Any

def project_root() -> Path:
    return Path(__file__).resolve().parent.parent


def ensure_local_vlm_python() -> None:
    try:
        import openvino  # noqa: F401
        import openvino_genai  # noqa: F401
        return
    except ImportError:
        pass

    root = project_root()
    local_python = root / ".venv-vlm" / "bin" / "python"
    current_python = Path(sys.executable)
    if local_python.is_file() and current_python != local_python:
        os.execv(str(local_python), [str(local_python), __file__, *sys.argv[1:]])


ensure_local_vlm_python()

import vision_vlm as vv


def resolve_model_dir(model_dir: Path | None) -> Path:
    if model_dir is not None:
        return model_dir.resolve()

    root = project_root()
    candidates = [
        root / "models" / "Qwen2.5-VL-3B-Instruct-ov-int4",
    ]
    for path in candidates:
        if (path / "openvino_language_model.xml").is_file():
            return path.resolve()
    raise SystemExit("No local OpenVINO VLM model found. Pass --model-dir or install the local 3B int4 model.")


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def resolve_frames_dir(path: Path) -> Path:
    path = path.resolve()
    if path.is_dir():
        frames = path / "frames"
        if frames.is_dir():
            return frames
        if any(p.suffix.lower() in {".png", ".bmp", ".jpg", ".jpeg"} for p in path.iterdir()):
            return path
        result_json = path / "result.json"
        if result_json.is_file():
            return resolve_frames_dir(result_json)
        metadata_json = path / "metadata.json"
        if metadata_json.is_file():
            nested = path / "frames"
            if nested.is_dir():
                return nested
    if path.is_file() and path.suffix.lower() == ".json":
        payload = load_json(path)
        frames_dir = payload.get("paths", {}).get("frames_dir")
        if frames_dir:
            return Path(frames_dir).resolve()
    raise FileNotFoundError(f"Unable to resolve frames dir from {path}")


def resolve_scene_id(path: Path, fallback: str | None = None) -> str | None:
    path = path.resolve()
    json_path = path if path.is_file() else None
    if json_path is None:
        for candidate in (path / "result.json", path / "metadata.json"):
            if candidate.is_file():
                json_path = candidate
                break
    if json_path is not None:
        payload = load_json(json_path)
        scene = payload.get("scene", {})
        ads_name = scene.get("ads_name") or payload.get("ads_name")
        tag = scene.get("tag") or payload.get("tag")
        if ads_name and tag is not None:
            return f"{ads_name}-{tag}"
    return fallback


def sample_scene_frames(frames_dir: Path, *, prefer_scene_content: bool, skip_front_fraction: float = 0.0) -> list[Path]:
    frames = vv.collect_frame_paths(frames_dir)
    if prefer_scene_content:
        filtered = vv._filter_scene_content_frames(frames, yellow_threshold=1.5)
        if filtered:
            frames = filtered
    if frames and skip_front_fraction > 0.0:
        skip = min(len(frames) - 1, int(len(frames) * skip_front_fraction))
        frames = frames[skip:]
    return frames


def case_label(compare: dict[str, Any]) -> str:
    query_screen = compare.get("query_screen_type")
    missing = compare.get("character_diff", {}).get("missing_characters", [])
    same = bool(compare.get("same_scene_state", False))
    if not same and (query_screen in {"black", "ocean"} or missing):
        return "broken_scene_output"
    if same and query_screen not in {"black", "ocean"}:
        return "correct_scene_content"
    return "uncertain"


def ratio(part: int, total: int) -> float:
    if total <= 0:
        return 0.0
    return float(part) / float(total)


def run_pack(args: argparse.Namespace) -> int:
    root = project_root()
    manifest = load_json(args.manifest)
    outdir = args.outdir.resolve()
    outdir.mkdir(parents=True, exist_ok=True)
    model_dir = resolve_model_dir(args.model_dir)

    _, openvino_genai = vv._require_openvino()
    pipe = openvino_genai.VLMPipeline(str(model_dir), args.device)

    rows = []
    passed = 0
    expected_counts: Counter[str] = Counter()
    actual_counts: Counter[str] = Counter()
    confusion: dict[str, Counter[str]] = {}
    family_stats: dict[str, dict[str, int]] = {}
    source_kind_stats: dict[str, dict[str, int]] = {}
    for index, case in enumerate(manifest.get("cases", []), start=1):
        case_id = case["case_id"]
        reference_image = (root / case["reference_image"]).resolve()
        query_image = (root / case["query_image"]).resolve()
        out_json = outdir / "cases" / f"{case_id}.json"
        out_json.parent.mkdir(parents=True, exist_ok=True)
        print(f"[{index}] {case_id}", flush=True)
        vv.compare_images(
            model_dir,
            reference_image,
            query_image,
            out_json,
            scene_id=case.get("scene_id"),
            device=args.device,
            max_new_tokens=args.max_new_tokens,
            image_size=args.image_size,
            pipe=pipe,
        )
        compare = load_json(out_json)
        actual = case_label(compare)
        expected = case["expected_label"]
        expected_counts[expected] += 1
        actual_counts[actual] += 1
        confusion.setdefault(expected, Counter())[actual] += 1
        ok = actual == expected
        if ok:
            passed += 1
        family = case.get("family") or "unknown"
        source_kind = case.get("source_kind") or "unknown"
        family_row = family_stats.setdefault(family, {"case_count": 0, "passed_count": 0})
        family_row["case_count"] += 1
        family_row["passed_count"] += 1 if ok else 0
        source_row = source_kind_stats.setdefault(source_kind, {"case_count": 0, "passed_count": 0})
        source_row["case_count"] += 1
        source_row["passed_count"] += 1 if ok else 0
        rows.append(
            {
                "case_id": case_id,
                "scene_id": case.get("scene_id"),
                "family": family,
                "source_kind": source_kind,
                "expected_label": expected,
                "actual_label": actual,
                "passed": ok,
                "same_scene_state": bool(compare.get("same_scene_state", False)),
                "reference_screen_type": compare.get("reference_screen_type"),
                "query_screen_type": compare.get("query_screen_type"),
                "missing_characters": compare.get("character_diff", {}).get("missing_characters", []),
                "summary": compare.get("summary", ""),
                "analysis_json": os.path.relpath(out_json, outdir),
            }
        )

    summary = {
        "model_dir": str(model_dir),
        "selected_model_dir": str(model_dir),
        "device": args.device,
        "manifest": str(args.manifest.resolve()),
        "case_count": len(rows),
        "passed_count": passed,
        "failed_count": len(rows) - passed,
        "accuracy": ratio(passed, len(rows)),
        "all_passed": passed == len(rows),
        "expected_label_counts": dict(expected_counts),
        "actual_label_counts": dict(actual_counts),
        "confusion": {key: dict(value) for key, value in confusion.items()},
        "family_stats": {
            key: {
                **value,
                "accuracy": ratio(value["passed_count"], value["case_count"]),
            }
            for key, value in sorted(family_stats.items())
        },
        "source_kind_stats": {
            key: {
                **value,
                "accuracy": ratio(value["passed_count"], value["case_count"]),
            }
            for key, value in sorted(source_kind_stats.items())
        },
        "cases": rows,
    }
    (outdir / "validation-summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return 0 if summary["all_passed"] else 1


def run_scene_fix(args: argparse.Namespace) -> int:
    out_json = args.out_json.resolve()
    out_json.parent.mkdir(parents=True, exist_ok=True)
    model_dir = resolve_model_dir(args.model_dir)

    reference_frames_dir = resolve_frames_dir(args.reference)
    query_frames_dir = resolve_frames_dir(args.result)
    scene_id = args.scene_id or resolve_scene_id(args.reference) or resolve_scene_id(args.result)

    reference_frames = sample_scene_frames(reference_frames_dir, prefer_scene_content=True, skip_front_fraction=0.2)
    query_frames = sample_scene_frames(query_frames_dir, prefer_scene_content=True)
    if not reference_frames:
        reference_frames = vv.collect_frame_paths(reference_frames_dir)
    if not query_frames:
        query_frames = vv.collect_frame_paths(query_frames_dir)

    pair_count = min(args.samples, len(reference_frames), len(query_frames))
    if pair_count <= 0:
        raise SystemExit("No frames available for VLM validation")

    reference_sel = vv.vc.evenly_sample(reference_frames, pair_count)
    query_sel = vv.vc.evenly_sample(query_frames, pair_count)
    bank = vv.load_bank(args.bank_dir)

    _, openvino_genai = vv._require_openvino()
    pipe = openvino_genai.VLMPipeline(str(model_dir), args.device)

    rows = []
    label_counts: Counter[str] = Counter()
    hint_scene_counts: Counter[str] = Counter()
    for index, (reference_image, query_image) in enumerate(zip(reference_sel, query_sel), start=1):
        compare_json = out_json.parent / "pairs" / f"{reference_image.stem}__vs__{query_image.stem}.json"
        compare_json.parent.mkdir(parents=True, exist_ok=True)
        print(f"[{index}/{pair_count}] {reference_image.name} vs {query_image.name}", flush=True)
        vv.compare_images(
            model_dir,
            reference_image,
            query_image,
            compare_json,
            scene_id=scene_id,
            device=args.device,
            max_new_tokens=args.max_new_tokens,
            image_size=args.image_size,
            pipe=pipe,
        )
        compare = load_json(compare_json)
        label = case_label(compare)
        label_counts[label] += 1
        hints = vv.reference_hints_for_image(query_image, bank, topk=min(3, args.topk))
        if hints:
            hint_scene_counts[hints[0]["scene_id"]] += 1
        rows.append(
            {
                "reference_image": str(reference_image),
                "query_image": str(query_image),
                "label": label,
                "same_scene_state": bool(compare.get("same_scene_state", False)),
                "reference_screen_type": compare.get("reference_screen_type"),
                "query_screen_type": compare.get("query_screen_type"),
                "missing_characters": compare.get("character_diff", {}).get("missing_characters", []),
                "summary": compare.get("summary", ""),
                "reference_hints": hints,
                "analysis_json": os.path.relpath(compare_json, out_json.parent),
            }
        )

    broken_threshold = max(1, math.ceil(pair_count * 0.6))
    correct_threshold = max(1, math.ceil(pair_count * 0.6))
    if label_counts["broken_scene_output"] >= broken_threshold:
        verdict = "broken_scene_output"
    elif label_counts["correct_scene_content"] >= correct_threshold:
        verdict = "correct_scene_content"
    else:
        verdict = "uncertain"

    summary = {
        "scene_id": scene_id,
        "model_dir": str(model_dir),
        "device": args.device,
        "reference": str(args.reference.resolve()),
        "result": str(args.result.resolve()),
        "reference_frames_dir": str(reference_frames_dir),
        "query_frames_dir": str(query_frames_dir),
        "bank_dir": str(args.bank_dir.resolve()) if args.bank_dir else None,
        "pair_count": pair_count,
        "verdict": verdict,
        "label_counts": dict(label_counts),
        "dominant_hint_scene": hint_scene_counts.most_common(1)[0][0] if hint_scene_counts else None,
        "dominant_hint_scene_count": hint_scene_counts.most_common(1)[0][1] if hint_scene_counts else 0,
        "pairs": rows,
    }
    out_json.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return 0


def parse_args() -> argparse.Namespace:
    root = project_root()
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)

    pack = sub.add_parser("pack", help="Run the representative validation scene pack")
    pack.add_argument("--model-dir", type=Path)
    pack.add_argument("--manifest", type=Path, default=root / "config" / "ps1" / "vlm-scene-pack.json")
    pack.add_argument("--outdir", type=Path, required=True)
    pack.add_argument("--device", type=str, default="CPU")
    pack.add_argument("--max-new-tokens", type=int, default=260)
    pack.add_argument("--image-size", type=int, default=256)

    scene_fix = sub.add_parser("scene-fix", help="Run a VLM scene-fix verdict against one result/reference pair")
    scene_fix.add_argument("--model-dir", type=Path)
    scene_fix.add_argument("--reference", type=Path, required=True)
    scene_fix.add_argument("--result", type=Path, required=True)
    scene_fix.add_argument("--out-json", type=Path, required=True)
    scene_fix.add_argument("--bank-dir", type=Path, default=root / "vision-artifacts" / "vision-reference-pipeline-current" / "reference-bank")
    scene_fix.add_argument("--scene-id", type=str)
    scene_fix.add_argument("--samples", type=int, default=3)
    scene_fix.add_argument("--topk", type=int, default=3)
    scene_fix.add_argument("--device", type=str, default="CPU")
    scene_fix.add_argument("--max-new-tokens", type=int, default=260)
    scene_fix.add_argument("--image-size", type=int, default=256)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.cmd == "pack":
        return run_pack(args)
    if args.cmd == "scene-fix":
        return run_scene_fix(args)
    raise SystemExit(f"Unknown command: {args.cmd}")


if __name__ == "__main__":
    raise SystemExit(main())
