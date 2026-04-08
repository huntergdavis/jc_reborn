#!/usr/bin/env python3
"""
Compare two captured scene sequences frame-by-frame on the shared normalized scene surface.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from functools import lru_cache
from pathlib import Path

from PIL import Image


def load_normalizer():
    helper_path = Path(__file__).with_name("normalize-scene-frame.py")
    spec = importlib.util.spec_from_file_location("normalize_scene_frame", helper_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load normalizer: {helper_path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def load_visual_detect():
    helper_path = Path(__file__).with_name("visual_detect.py")
    spec = importlib.util.spec_from_file_location("visual_detect", helper_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load visual detector: {helper_path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def resolve_result(path: Path) -> Path:
    if path.is_dir():
        candidate = path / "result.json"
        if candidate.is_file():
            return candidate
        candidate = path / "metadata.json"
        if candidate.is_file():
            return candidate
    return path


def resolve_capture_path(path_value: str, base_dir: Path) -> Path:
    path = Path(path_value)
    if path.is_absolute():
        return path.resolve()
    return (base_dir / path).resolve()


def load_capture(path: Path) -> tuple[Path, dict]:
    resolved = resolve_result(path)
    if resolved.is_dir():
        frames_dir = find_best_frame_dir(resolved)
        data = {
            "scene": {},
            "paths": {
                "frames_dir": str(frames_dir.resolve()) if frames_dir.is_dir() else "",
            },
            "outcome": {},
        }
        return resolved, data
    return resolved, json.loads(resolved.read_text(encoding="utf-8"))


def frame_key(path: Path) -> str:
    stem = path.stem
    if stem.startswith("frame_"):
        return stem.split("_", 1)[1]
    return stem


def frame_number_from_name(name: str | None) -> int | None:
    if not name:
        return None
    stem = Path(name).stem
    if stem.startswith("frame_"):
        stem = stem.split("_", 1)[1]
    try:
        return int(stem)
    except ValueError:
        return None


def load_frame_map(result: dict, result_path: Path) -> dict[str, Path]:
    frames_dir = result.get("paths", {}).get("frames_dir")
    candidate_dirs: list[Path] = []
    frame_map: dict[str, Path] = {}

    if frames_dir:
        candidate_dirs.append(resolve_capture_path(frames_dir, result_path.parent))

    parent = result_path.parent
    candidate_dirs.extend([parent / "frames", parent / "frames-png", result_path / "frames", result_path / "frames-png"])
    best_dir = find_best_frame_dir(result_path if result_path.is_dir() else parent)
    if best_dir.is_dir():
        candidate_dirs.append(best_dir)

    seen_dirs: set[Path] = set()
    for frame_dir in candidate_dirs:
        frame_dir = frame_dir.resolve()
        if frame_dir in seen_dirs or not frame_dir.is_dir():
            continue
        seen_dirs.add(frame_dir)
        for frame_path in sorted(frame_dir.glob("**/frame_*.*")):
            frame_map[frame_key(frame_path)] = frame_path
        if frame_map:
            break
    return frame_map


def find_best_frame_dir(root: Path) -> Path:
    direct_candidates = [root / "frames", root / "frames-png"]
    for candidate in direct_candidates:
        if candidate.is_dir():
            nested = discover_leaf_frame_dir(candidate)
            if nested is not None:
                return nested
            return candidate

    recursive: list[Path] = []
    for pattern in ("**/frames", "**/frames-png"):
        recursive.extend(path for path in root.glob(pattern) if path.is_dir())
    recursive = sorted(set(path.resolve() for path in recursive))
    for candidate in recursive:
        nested = discover_leaf_frame_dir(candidate)
        if nested is not None:
            return nested
    return root / "frames"


def discover_leaf_frame_dir(frame_dir: Path) -> Path | None:
    if any(frame_dir.glob("frame_*.*")):
        return frame_dir

    child_dirs = sorted(child for child in frame_dir.iterdir() if child.is_dir())
    for child in child_dirs:
        if any(child.glob("frame_*.*")):
            return child
    return None


def load_entry_frame_number(
    result: dict,
    frame_map: dict[str, Path],
    visual_detect,
    min_scene_frame: int = 0,
) -> int | None:
    marker_frame = result.get("outcome", {}).get("scene_start_frame")
    if marker_frame is None:
        marker_frame = result.get("scene_start_frame")
    if marker_frame is not None and marker_frame >= min_scene_frame:
        return marker_frame

    frame_name = result.get("outcome", {}).get("visual_entry_scene", {}).get("frame")
    fallback = frame_number_from_name(frame_name)
    if fallback is not None and fallback >= min_scene_frame:
        return fallback

    for key in sorted(frame_map):
        key_num = int(key)
        if key_num < min_scene_frame:
            continue
        analysis = visual_detect.analyze_screenshot(str(frame_map[key]), json_output=False)
        screen_type = analysis.screen_type.screen_type
        if screen_type in {"black", "title", "transition"}:
            continue
        return key_num
    if frame_map:
        return min(int(key) for key in frame_map if int(key) >= min_scene_frame)
    return None


def palette_index_diff_pixels(result_rgb: Image.Image, reference_rgb: Image.Image) -> int:
    paletted_ref = reference_rgb.convert("P", palette=Image.Palette.ADAPTIVE, colors=16)
    result_idx = result_rgb.quantize(palette=paletted_ref, dither=Image.Dither.NONE)
    reference_idx = reference_rgb.quantize(palette=paletted_ref, dither=Image.Dither.NONE)
    result_bytes = result_idx.tobytes()
    reference_bytes = reference_idx.tobytes()
    return sum(1 for a, b in zip(result_bytes, reference_bytes) if a != b)


def build_normalized_rgb_loader(frame_map: dict[str, Path], normalizer):
    @lru_cache(maxsize=None)
    def load(key: str) -> Image.Image:
        return normalizer.mask_scene(
            normalizer.normalize_image(frame_map[key])[0]
        ).convert("RGB")

    return load


def build_thumbnail_loader(rgb_loader, size: tuple[int, int] = (64, 45)):
    @lru_cache(maxsize=None)
    def load(key: str) -> Image.Image:
        return rgb_loader(key).resize(size, Image.Resampling.BILINEAR)

    return load


def find_verified_result_anchor(
    result_frames: dict[str, Path],
    reference_frames: dict[str, Path],
    visual_detect,
    result_normalized_rgb_for_key,
    reference_normalized_rgb_for_key,
    result_thumbnail_for_key,
    reference_thumbnail_for_key,
    max_diff: int,
    reference_window: int,
    min_result_scene_frame: int,
    min_reference_scene_frame: int,
    reference_anchor: int | None,
) -> tuple[int | None, int | None]:
    reference_candidates: list[str] = []
    if reference_anchor is not None:
        reference_candidates = [
            key for key in sorted(reference_frames)
            if int(key) >= max(min_reference_scene_frame, reference_anchor)
        ]
    else:
        for key in sorted(reference_frames):
            key_num = int(key)
            if key_num < min_reference_scene_frame:
                continue
            analysis = visual_detect.analyze_screenshot(str(reference_frames[key]), json_output=False)
            screen_type = analysis.screen_type.screen_type
            if screen_type in {"black", "title", "transition"}:
                continue
            reference_candidates.append(key)

    if not reference_candidates:
        return None, None
    reference_candidates = reference_candidates[:reference_window]

    eligible_result_keys = [
        key for key in sorted(result_frames)
        if int(key) >= min_result_scene_frame
    ]
    if not eligible_result_keys:
        return None, None

    def candidate_match(result_key: str) -> tuple[int | None, int | None]:
        result_key_num = int(result_key)

        # When the host/canonical reference already starts at scene frame 0,
        # the pixel diff test is the reliable anchor; rescanning thousands of
        # result frames through visual_detect is too expensive.
        if reference_anchor is None:
            analysis = visual_detect.analyze_screenshot(str(result_frames[result_key]), json_output=False)
            screen_type = analysis.screen_type.screen_type
            if screen_type in {"black", "title", "transition"}:
                return None, None

        result_thumb = result_thumbnail_for_key(result_key)
        best_ref_key = None
        best_thumb_diff = None
        for ref_key in reference_candidates:
            diff = palette_index_diff_pixels(result_thumb, reference_thumbnail_for_key(ref_key))
            if best_thumb_diff is None or diff < best_thumb_diff:
                best_thumb_diff = diff
                best_ref_key = ref_key
        if best_ref_key is not None:
            full_diff = palette_index_diff_pixels(
                result_normalized_rgb_for_key(result_key),
                reference_normalized_rgb_for_key(best_ref_key),
            )
            if full_diff <= max_diff:
                return result_key_num, int(best_ref_key)
        return None, None

    coarse_step = 1
    if reference_anchor is not None and len(eligible_result_keys) > 600:
        coarse_step = 30
    if reference_anchor is not None and len(eligible_result_keys) > 1800:
        coarse_step = 60

    coarse_candidates = eligible_result_keys[::coarse_step]
    for coarse_index, result_key in enumerate(coarse_candidates):
        result_match, reference_match = candidate_match(result_key)
        if result_match is None:
            continue
        if coarse_step == 1:
            return result_match, reference_match

        start = max(0, coarse_index * coarse_step - (coarse_step - 1))
        end = min(len(eligible_result_keys), coarse_index * coarse_step + 1)
        for refine_key in eligible_result_keys[start:end]:
            result_match, reference_match = candidate_match(refine_key)
            if result_match is not None:
                return result_match, reference_match

    return None, None


def main() -> int:
    ap = argparse.ArgumentParser(description="Compare two captured scene sequences frame-by-frame")
    ap.add_argument("--result", required=True, help="Result sequence result.json or containing directory")
    ap.add_argument("--reference", required=True, help="Reference sequence result.json or containing directory")
    ap.add_argument("--scene-entry-align", action="store_true",
                    help="Align frame numbers using visual_entry_scene anchors before comparison")
    ap.add_argument("--entry-max-diff", type=int, default=5000,
                    help="Maximum palette-index diff allowed for a result frame to qualify as a scene-entry anchor")
    ap.add_argument("--entry-reference-window", type=int, default=30,
                    help="How many early non-title reference frames to consider when validating scene-entry anchors")
    ap.add_argument("--min-result-scene-frame", type=int, default=0,
                    help="Minimum result frame number eligible to become a verified scene-entry anchor")
    ap.add_argument("--min-reference-scene-frame", type=int, default=0,
                    help="Minimum reference frame number eligible to become a scene-entry anchor")
    ap.add_argument("--scene-window-only", action="store_true",
                    help="Only compare result frames that align into the reference scene frame range")
    ap.add_argument("--json", action="store_true", help="Emit JSON")
    args = ap.parse_args()

    normalizer = load_normalizer()
    visual_detect = load_visual_detect()
    result_path, result = load_capture(Path(args.result).resolve())
    reference_path, reference = load_capture(Path(args.reference).resolve())

    result_frames = load_frame_map(result, result_path)
    reference_frames = load_frame_map(reference, reference_path)
    result_normalized_rgb_for_key = build_normalized_rgb_loader(result_frames, normalizer)
    reference_normalized_rgb_for_key = build_normalized_rgb_loader(reference_frames, normalizer)
    result_thumbnail_for_key = build_thumbnail_loader(result_normalized_rgb_for_key)
    reference_thumbnail_for_key = build_thumbnail_loader(reference_normalized_rgb_for_key)
    frame_offset = 0
    alignment_mode = "raw"
    result_anchor = None
    reference_anchor = None

    if args.scene_entry_align:
        reference_anchor = load_entry_frame_number(
            reference,
            reference_frames,
            visual_detect,
            args.min_reference_scene_frame,
        )
        result_anchor, matched_reference_anchor = find_verified_result_anchor(
            result_frames,
            reference_frames,
            visual_detect,
            result_normalized_rgb_for_key,
            reference_normalized_rgb_for_key,
            result_thumbnail_for_key,
            reference_thumbnail_for_key,
            args.entry_max_diff,
            args.entry_reference_window,
            args.min_result_scene_frame,
            args.min_reference_scene_frame,
            reference_anchor,
        )
        if reference_anchor is None and matched_reference_anchor is not None:
            reference_anchor = matched_reference_anchor
        elif matched_reference_anchor is not None:
            reference_anchor = matched_reference_anchor
        if result_anchor is None or reference_anchor is None:
            payload = {
                "result": str(result_path),
                "reference": str(reference_path),
                "alignment_mode": "scene_entry",
                "result_entry_frame": result_anchor,
                "reference_entry_frame": reference_anchor,
                "min_result_scene_frame": args.min_result_scene_frame,
                "min_reference_scene_frame": args.min_reference_scene_frame,
                "error": "missing verified scene_entry anchor",
                "result_frame_count": len(result_frames),
                "reference_frame_count": len(reference_frames),
                "common_frame_count": 0,
                "result_only_frame_count": len(result_frames),
                "reference_only_frame_count": len(reference_frames),
                "result_coverage_ratio": 0.0 if result_frames else None,
                "reference_coverage_ratio": 0.0 if reference_frames else None,
                "result_only_frames": sorted(result_frames),
                "reference_only_frames": sorted(reference_frames),
                "average_palette_index_diff_pixels": None,
                "worst_frame": None,
                "all_frames_match": False,
                "frames": [],
            }
            if args.json:
                print(json.dumps(payload, indent=2))
            else:
                print("error: missing visual_entry_scene anchor")
            return 0
        frame_offset = result_anchor - reference_anchor
        alignment_mode = "scene_entry"

    matched_reference_keys: set[str] = set()
    common_names: list[str] = []
    result_only: list[str] = []
    result_pre_scene: list[str] = []
    result_post_scene: list[str] = []
    result_in_scene_unmatched: list[str] = []
    reference_numbers = sorted(int(key) for key in reference_frames)
    reference_scene_start = min(reference_numbers) if reference_numbers else None
    reference_scene_end = max(reference_numbers) if reference_numbers else None
    result_scene_candidates = 0

    for result_key in sorted(result_frames):
        result_num = int(result_key)
        ref_num = result_num
        if args.scene_entry_align:
            ref_num = result_num - frame_offset

        if (
            args.scene_window_only
            and reference_scene_start is not None
            and reference_scene_end is not None
        ):
            if ref_num < reference_scene_start:
                result_pre_scene.append(result_key)
                continue
            if ref_num > reference_scene_end:
                result_post_scene.append(result_key)
                continue

        ref_key = f"{ref_num:05d}"
        if ref_key in reference_frames:
            common_names.append(result_key)
            matched_reference_keys.add(ref_key)
            if (
                reference_scene_start is not None
                and reference_scene_end is not None
                and reference_scene_start <= ref_num <= reference_scene_end
            ):
                result_scene_candidates += 1
        else:
            result_only.append(result_key)
            if (
                reference_scene_start is not None
                and reference_scene_end is not None
                and reference_scene_start <= ref_num <= reference_scene_end
            ):
                result_in_scene_unmatched.append(result_key)
                result_scene_candidates += 1

    reference_only = sorted(
        key for key in reference_frames
        if key not in matched_reference_keys
        and (
            reference_scene_start is None
            or reference_scene_end is None
            or reference_scene_start <= int(key) <= reference_scene_end
        )
    )

    rows = []
    worst = None
    total_diff = 0

    for key in common_names:
        result_frame = result_frames[key]
        reference_key = key
        if args.scene_entry_align:
            reference_key = f"{int(key) - frame_offset:05d}"
        reference_frame = reference_frames[reference_key]
        result_img = result_normalized_rgb_for_key(key)
        reference_img = reference_normalized_rgb_for_key(reference_key)
        result_sha = normalizer.sha256_bytes(result_img.tobytes())
        reference_sha = normalizer.sha256_bytes(reference_img.tobytes())
        diff = palette_index_diff_pixels(result_img, reference_img)
        row = {
            "frame": key,
            "reference_frame": reference_key,
            "result_frame_name": result_frame.name,
            "reference_frame_name": reference_frame.name,
            "status": "same" if result_sha == reference_sha else "changed",
            "palette_index_diff_pixels": diff,
            "result_frame": str(result_frame.resolve()),
            "reference_frame": str(reference_frame.resolve()),
        }
        rows.append(row)
        total_diff += diff
        if worst is None or diff > worst["palette_index_diff_pixels"]:
            worst = row

    all_frames_same = bool(common_names) and all(r["status"] == "same" for r in rows)
    if not common_names:
        verdict = "ALIGNMENT_FAILED"
    elif not all_frames_same:
        verdict = "PIXEL_MISMATCH"
    elif result_in_scene_unmatched:
        verdict = "TIMING_MISMATCH"
    else:
        verdict = "MATCH"

    payload = {
        "result": str(result_path),
        "reference": str(reference_path),
        "alignment_mode": alignment_mode,
        "frame_offset": frame_offset,
        "result_entry_frame": result_anchor,
        "reference_entry_frame": reference_anchor,
        "min_result_scene_frame": args.min_result_scene_frame,
        "min_reference_scene_frame": args.min_reference_scene_frame,
        "reference_scene_start_frame": reference_scene_start,
        "reference_scene_end_frame": reference_scene_end,
        "result_frame_count": len(result_frames),
        "reference_frame_count": len(reference_frames),
        "result_scene_candidate_count": result_scene_candidates,
        "common_frame_count": len(common_names),
        "result_only_frame_count": len(result_only),
        "reference_only_frame_count": len(reference_only),
        "result_pre_scene_frame_count": len(result_pre_scene),
        "result_post_scene_frame_count": len(result_post_scene),
        "result_in_scene_unmatched_count": len(result_in_scene_unmatched),
        "result_coverage_ratio": (
            len(common_names) / result_scene_candidates
        ) if result_scene_candidates else None,
        "result_total_coverage_ratio": (len(common_names) / len(result_frames)) if result_frames else None,
        "reference_coverage_ratio": (len(common_names) / len(reference_frames)) if reference_frames else None,
        "result_only_frames": result_only,
        "result_pre_scene_frames": result_pre_scene,
        "result_post_scene_frames": result_post_scene,
        "result_in_scene_unmatched_frames": result_in_scene_unmatched,
        "reference_only_frames": reference_only,
        "average_palette_index_diff_pixels": (total_diff / len(common_names)) if common_names else None,
        "worst_frame": worst,
        "all_frames_match": all_frames_same,
        "verdict": verdict,
        "frames": rows,
    }

    if args.json:
        print(json.dumps(payload, indent=2))
    else:
        print(f"result_frame_count: {payload['result_frame_count']}")
        print(f"reference_frame_count: {payload['reference_frame_count']}")
        print(f"reference_scene_start_frame: {payload['reference_scene_start_frame']}")
        print(f"reference_scene_end_frame: {payload['reference_scene_end_frame']}")
        print(f"common_frame_count: {payload['common_frame_count']}")
        print(f"result_pre_scene_frame_count: {payload['result_pre_scene_frame_count']}")
        print(f"result_post_scene_frame_count: {payload['result_post_scene_frame_count']}")
        print(f"result_in_scene_unmatched_count: {payload['result_in_scene_unmatched_count']}")
        print(f"result_coverage_ratio: {payload['result_coverage_ratio']}")
        print(f"result_total_coverage_ratio: {payload['result_total_coverage_ratio']}")
        print(f"reference_coverage_ratio: {payload['reference_coverage_ratio']}")
        print(f"all_frames_match: {payload['all_frames_match']}")
        print(f"verdict: {payload['verdict']}")
        print(f"average_palette_index_diff_pixels: {payload['average_palette_index_diff_pixels']}")
        if worst is not None:
            print(f"worst_frame: {worst['frame']}")
            print(f"worst_palette_index_diff_pixels: {worst['palette_index_diff_pixels']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
