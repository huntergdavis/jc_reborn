#!/usr/bin/env python3
"""
Compare a raw screenshot against a host scene sequence and find the closest
matching reference frame after shared normalization/masking.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path

from PIL import Image


def load_normalizer():
    helper_path = Path(__file__).with_name("normalize-scene-frame.py")
    spec = importlib.util.spec_from_file_location("normalize_scene_frame", helper_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load normalizer: {helper_path}")
    module = importlib.util.module_from_spec(spec)
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


def palette_index_diff_pixels(result_rgb: Image.Image, reference_rgb: Image.Image) -> int:
    paletted_ref = reference_rgb.convert("P", palette=Image.Palette.ADAPTIVE, colors=16)
    result_idx = result_rgb.quantize(palette=paletted_ref, dither=Image.Dither.NONE)
    reference_idx = reference_rgb.quantize(palette=paletted_ref, dither=Image.Dither.NONE)
    result_bytes = result_idx.tobytes()
    reference_bytes = reference_idx.tobytes()
    return sum(1 for a, b in zip(result_bytes, reference_bytes) if a != b)


def load_frame_paths(result: dict, result_path: Path) -> list[Path]:
    frames_dir = result.get("paths", {}).get("frames_dir")
    if frames_dir:
        frame_paths = sorted(resolve_capture_path(frames_dir, result_path.parent).glob("**/frame_*.*"))
        if frame_paths:
            return frame_paths

    frame_paths = []
    outcome = result.get("outcome", {})
    for key in ("visual_best", "visual_entry_scene", "visual_last_scene"):
        frame_path = outcome.get(key, {}).get("frame_path")
        if frame_path:
            p = resolve_capture_path(frame_path, result_path.parent)
            if p.is_file():
                frame_paths.append(p)
    deduped = []
    seen = set()
    for p in frame_paths:
        if p not in seen:
            seen.add(p)
            deduped.append(p)
    return deduped


def main() -> int:
    ap = argparse.ArgumentParser(description="Compare a screenshot against a host scene sequence")
    ap.add_argument("--image", required=True, help="Raw host or DuckStation screenshot")
    ap.add_argument("--sequence", required=True, help="Host scene result.json or containing directory")
    ap.add_argument("--json", action="store_true", help="Emit JSON")
    args = ap.parse_args()

    normalizer = load_normalizer()
    image_path = Path(args.image).resolve()
    sequence_path = resolve_result(Path(args.sequence).resolve())
    sequence = json.loads(sequence_path.read_text(encoding="utf-8"))

    normalized_input, input_meta = normalizer.normalize_image(image_path)
    masked_input = normalizer.mask_scene(normalized_input).convert("RGB")
    input_sha = normalizer.sha256_bytes(masked_input.tobytes())

    frame_paths = load_frame_paths(sequence, sequence_path)
    best = None

    for frame_path in frame_paths:
        normalized_ref, _ref_meta = normalizer.normalize_image(frame_path)
        masked_ref = normalizer.mask_scene(normalized_ref).convert("RGB")
        diff = palette_index_diff_pixels(masked_input, masked_ref)
        ref_sha = normalizer.sha256_bytes(masked_ref.tobytes())
        row = {
            "frame": frame_path.name,
            "frame_path": str(frame_path.resolve()),
            "scene_pixel_sha256": ref_sha,
            "palette_index_diff_pixels": diff,
            "status": "same" if ref_sha == input_sha else "changed",
        }
        if best is None or diff < best["palette_index_diff_pixels"]:
            best = row

    payload = {
        "image": str(image_path),
        "sequence": str(sequence_path),
        "source_normalization": input_meta,
        "source_scene_pixel_sha256": input_sha,
        "frames_considered": len(frame_paths),
        "best_match": best,
    }

    if args.json:
        print(json.dumps(payload, indent=2))
    else:
        print(f"image: {image_path}")
        print(f"sequence: {sequence_path}")
        print(f"frames_considered: {len(frame_paths)}")
        if best is not None:
            print(f"best_frame: {best['frame']}")
            print(f"best_status: {best['status']}")
            print(f"best_palette_index_diff_pixels: {best['palette_index_diff_pixels']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
