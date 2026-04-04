#!/usr/bin/env python3
"""
Compare a regtest scene result against a captured reference metadata file.

This is the bridge between the live PS1 certification surface and future
PC/host reference captures: compare masked scene hashes for best/entry/last
scene frames rather than raw final-state hashes.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    Image = None


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def load_normalizer():
    helper_path = Path(__file__).with_name("normalize-scene-frame.py")
    spec = importlib.util.spec_from_file_location("normalize_scene_frame", helper_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load normalizer: {helper_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def resolve_reference(path: Path) -> Path:
    if path.is_dir():
        candidate = path / "metadata.json"
        if candidate.is_file():
            return candidate
    return path


def get_nested(d: dict, *keys, default=None):
    cur = d
    for key in keys:
        if not isinstance(cur, dict) or key not in cur:
            return default
        cur = cur[key]
    return cur


def scene_hash(d: dict, section: str) -> str:
    return (
        get_nested(d, section, "frame_scene_pixel_sha256", default="")
        or get_nested(d, section, "frame_pixel_sha256", default="")
        or get_nested(d, section, "frame_sha256", default="")
        or ""
    )


def build_frame_result(frame_path_value: str) -> dict:
    normalizer = load_normalizer()
    frame_path = Path(frame_path_value).resolve()
    normalized, meta = normalizer.normalize_image(frame_path)
    masked = normalizer.mask_scene(normalized)
    frame_entry = {
        "frame": frame_path.name,
        "frame_path": str(frame_path),
        "frame_pixel_sha256": normalizer.sha256_bytes(normalized.tobytes()),
        "frame_scene_pixel_sha256": normalizer.sha256_bytes(masked.tobytes()),
        "normalization": meta,
    }
    return {
        "scene": {},
        "outcome": {
            "visual_best": frame_entry,
            "visual_entry_scene": frame_entry,
            "visual_last_scene": frame_entry,
        },
    }


def frame_path(d: dict, section: str) -> str:
    return get_nested(d, section, "frame_path", default="") or ""


def masked_scene_rgb(path: Path):
    if Image is None or not path.is_file():
        return None
    normalizer = load_normalizer()
    normalized, _meta = normalizer.normalize_image(path)
    masked = normalizer.mask_scene(normalized)
    return masked.convert("RGB")


def palette_index_diff_pixels(result_frame: Path, reference_frame: Path) -> int | None:
    if Image is None or not result_frame.is_file() or not reference_frame.is_file():
        return None

    result_rgb = masked_scene_rgb(result_frame)
    reference_rgb = masked_scene_rgb(reference_frame)
    if result_rgb is None or reference_rgb is None:
        return None
    if result_rgb.size != reference_rgb.size:
        return None

    # Quantize both images through the reference frame's palette so small
    # RGB capture differences do not dominate cross-platform comparisons.
    paletted_ref = reference_rgb.convert("P", palette=Image.Palette.ADAPTIVE, colors=16)
    result_idx = result_rgb.quantize(palette=paletted_ref, dither=Image.Dither.NONE)
    reference_idx = reference_rgb.quantize(palette=paletted_ref, dither=Image.Dither.NONE)

    result_bytes = result_idx.tobytes()
    reference_bytes = reference_idx.tobytes()
    return sum(1 for a, b in zip(result_bytes, reference_bytes) if a != b)


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Compare a regtest result.json against reference metadata.json"
    )
    ap.add_argument("--result", help="Path to regtest result.json")
    ap.add_argument("--result-frame", help="Path to a raw host or DuckStation screenshot")
    ap.add_argument(
        "--reference",
        required=True,
        help="Path to reference metadata.json or its containing directory",
    )
    ap.add_argument("--json", action="store_true", help="Emit JSON")
    args = ap.parse_args()

    if not args.result and not args.result_frame:
        ap.error("one of --result or --result-frame is required")

    result_path = Path(args.result).resolve() if args.result else None
    reference_path = resolve_reference(Path(args.reference).resolve())

    if result_path is not None:
        result = load_json(result_path)
    else:
        result = build_frame_result(args.result_frame)
    reference = load_json(reference_path)

    result_outcome = result.get("outcome", {})

    comparisons = {}
    for section in ("visual_best", "visual_entry_scene", "visual_last_scene"):
        result_hash = scene_hash(result_outcome, section)
        reference_hash = scene_hash(reference, section)
        result_frame = frame_path(result_outcome, section)
        reference_frame = frame_path(reference, section)
        comparisons[section] = {
            "result": result_hash,
            "reference": reference_hash,
            "status": "same" if result_hash and result_hash == reference_hash else "changed",
            "palette_index_diff_pixels": palette_index_diff_pixels(
                Path(result_frame), Path(reference_frame)
            ),
            "result_frame": result_frame,
            "reference_frame": reference_frame,
        }

    verdict = "MATCH"
    if comparisons["visual_last_scene"]["status"] != "same":
        verdict = "LASTSCN_CHANGED"
    elif comparisons["visual_entry_scene"]["status"] != "same":
        verdict = "ENTRY_CHANGED"
    elif comparisons["visual_best"]["status"] != "same":
        verdict = "BEST_CHANGED"

    payload = {
        "scene": {
            "result": result.get("scene", {}),
            "reference": {
                "ads_name": reference.get("ads_name"),
                "tag": reference.get("tag"),
                "scene_index": reference.get("scene_index"),
                "boot_string": reference.get("boot_string"),
                "forced_seed": reference.get("forced_seed"),
            },
        },
        "comparisons": comparisons,
        "verdict": verdict,
        "paths": {
            "result": str(result_path) if result_path is not None else None,
            "result_frame": str(Path(args.result_frame).resolve()) if args.result_frame else None,
            "reference": str(reference_path),
        },
    }

    if args.json:
        json.dump(payload, sys.stdout, indent=2)
        sys.stdout.write("\n")
    else:
        print(f"Scene:    {result.get('scene', {}).get('ads_name')} {result.get('scene', {}).get('tag')}")
        print(f"Result:   {result_path}")
        print(f"Reference:{reference_path}")
        for section in ("visual_best", "visual_entry_scene", "visual_last_scene"):
            short = section.replace("visual_", "").replace("_scene", "").upper()
            status = comparisons[section]["status"]
            pal_diff = comparisons[section]["palette_index_diff_pixels"]
            extra = "" if pal_diff is None else f"  paldiff={pal_diff}"
            print(f"{short:8} {status:7} {comparisons[section]['result'][:16]} -> {comparisons[section]['reference'][:16]}{extra}")
        print(f"Verdict:  {verdict}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
