#!/usr/bin/env python3
"""
Normalize a captured frame into a comparable 640x480 scene image.

Host captures are already native 640x480 and pass through unchanged.
DuckStation fullscreen/window captures are cropped to the active 4:3 viewport
and resized back to 640x480 so they can be compared against host references.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image

NATIVE_SIZE = (640, 480)
SCENE_SIZE = (640, 448)
SCENE_CROP = (0, 16, 640, 464)
MASK_RECTS = (
    (0, 0, 110, 260),
    (500, 124, 618, 344),
    (548, 16, 616, 84),
)


def find_viewport_box(rgb_img: Image.Image) -> tuple[int, int, int, int]:
    step = 2 if max(rgb_img.width, rgb_img.height) > 1000 else 1
    xs: list[int] = []
    ys: list[int] = []
    px = rgb_img.load()

    for y in range(0, rgb_img.height, step):
        for x in range(0, rgb_img.width, step):
            r, g, b = px[x, y]
            chroma = max(r, g, b) - min(r, g, b)
            if chroma >= 28 and max(r, g, b) >= 48:
                xs.append(x)
                ys.append(y)

    if not xs or not ys:
        return (0, 0, rgb_img.width, rgb_img.height)

    left = max(0, min(xs) - 12)
    top = max(0, min(ys) - 12)
    right = min(rgb_img.width, max(xs) + 13)
    bottom = min(rgb_img.height, max(ys) + 13)

    box_w = right - left
    box_h = bottom - top
    if box_w <= 0 or box_h <= 0:
        return (0, 0, rgb_img.width, rgb_img.height)

    target_w = max(box_w, int(round(box_h * (4.0 / 3.0))))
    target_h = max(box_h, int(round(target_w * (3.0 / 4.0))))
    cx = (left + right) // 2
    cy = (top + bottom) // 2

    left = cx - (target_w // 2)
    top = cy - (target_h // 2)
    right = left + target_w
    bottom = top + target_h

    if left < 0:
        right -= left
        left = 0
    if top < 0:
        bottom -= top
        top = 0
    if right > rgb_img.width:
        shift = right - rgb_img.width
        left = max(0, left - shift)
        right = rgb_img.width
    if bottom > rgb_img.height:
        shift = bottom - rgb_img.height
        top = max(0, top - shift)
        bottom = rgb_img.height

    return (left, top, right, bottom)


def normalize_image(src: Path) -> tuple[Image.Image, dict]:
    with Image.open(src) as img:
        rgb = img.convert("RGB")
        src_size = rgb.size

        if src_size == NATIVE_SIZE:
            normalized = rgb.copy()
            viewport = (0, 0, rgb.width, rgb.height)
            mode = "native"
        elif src_size == SCENE_SIZE:
            normalized = Image.new("RGB", NATIVE_SIZE, (0, 0, 0))
            normalized.paste(rgb, (0, 16))
            viewport = (0, 0, rgb.width, rgb.height)
            mode = "scene_native_padded"
        else:
            viewport = find_viewport_box(rgb)
            cropped = rgb.crop(viewport)
            normalized = cropped.resize(NATIVE_SIZE, Image.Resampling.NEAREST)
            mode = "viewport_resized"

    meta = {
        "source_size": list(src_size),
        "normalized_size": list(normalized.size),
        "viewport_box": list(viewport),
        "mode": mode,
    }
    return normalized, meta


def mask_scene(img: Image.Image) -> Image.Image:
    rgba = img.convert("RGBA")
    rgba = rgba.crop(SCENE_CROP)
    px = rgba.load()
    for x0, y0, x1, y1 in MASK_RECTS:
        for y in range(y0, min(y1, rgba.height)):
            for x in range(x0, min(x1, rgba.width)):
                px[x, y] = (0, 0, 0, 255)
    return rgba


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description="Normalize a host or DuckStation frame for comparison.")
    ap.add_argument("image", help="Input frame path")
    ap.add_argument("--output", help="Optional output image path")
    ap.add_argument("--json", action="store_true", help="Emit JSON metadata to stdout")
    args = ap.parse_args()

    src = Path(args.image).resolve()
    normalized, meta = normalize_image(src)
    masked = mask_scene(normalized)

    if args.output:
        out = Path(args.output).resolve()
        out.parent.mkdir(parents=True, exist_ok=True)
        normalized.save(out)
        meta["output_path"] = str(out)

    meta["input_path"] = str(src)
    meta["normalized_pixel_sha256"] = sha256_bytes(normalized.tobytes())
    meta["scene_pixel_sha256"] = sha256_bytes(masked.tobytes())

    if args.json:
        print(json.dumps(meta, indent=2))
    else:
        print(f"input:      {src}")
        print(f"mode:       {meta['mode']}")
        print(f"source:     {tuple(meta['source_size'])}")
        print(f"viewport:   {tuple(meta['viewport_box'])}")
        print(f"normalized: {tuple(meta['normalized_size'])}")
        print(f"scene_sha:  {meta['scene_pixel_sha256']}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
