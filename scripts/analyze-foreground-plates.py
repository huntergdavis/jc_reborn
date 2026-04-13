#!/usr/bin/env python3

import argparse
import json
import statistics
import zlib
from pathlib import Path

from PIL import Image


def parse_rgb(value: str) -> tuple[int, int, int]:
    raw = value.strip().lower().replace("#", "")
    if len(raw) != 6:
        raise argparse.ArgumentTypeError("expected RRGGBB")
    return tuple(int(raw[i:i + 2], 16) for i in (0, 2, 4))


def find_bbox(img: Image.Image, key_rgb: tuple[int, int, int]):
    pixels = img.load()
    width, height = img.size
    min_x = width
    min_y = height
    max_x = -1
    max_y = -1

    for y in range(height):
        for x in range(width):
            if pixels[x, y][:3] != key_rgb:
                if x < min_x:
                    min_x = x
                if y < min_y:
                    min_y = y
                if x > max_x:
                    max_x = x
                if y > max_y:
                    max_y = y

    if max_x < min_x or max_y < min_y:
        return None

    return {
        "x": min_x,
        "y": min_y,
        "width": max_x - min_x + 1,
        "height": max_y - min_y + 1,
    }


def crop_pixels(img: Image.Image, bbox):
    if bbox is None:
        return b"", 0

    crop = img.crop((
        bbox["x"],
        bbox["y"],
        bbox["x"] + bbox["width"],
        bbox["y"] + bbox["height"],
    )).convert("RGBA")
    colors = crop.getcolors(maxcolors=1 << 24) or []
    return crop.tobytes(), len(colors)


def delta_bbox(prev: Image.Image | None, current: Image.Image):
    if prev is None or prev.size != current.size:
        return {"x": 0, "y": 0, "width": current.size[0], "height": current.size[1]}

    prev_pixels = prev.load()
    curr_pixels = current.load()
    width, height = current.size
    min_x = width
    min_y = height
    max_x = -1
    max_y = -1

    for y in range(height):
        for x in range(width):
            if prev_pixels[x, y] != curr_pixels[x, y]:
                if x < min_x:
                    min_x = x
                if y < min_y:
                    min_y = y
                if x > max_x:
                    max_x = x
                if y > max_y:
                    max_y = y

    if max_x < min_x or max_y < min_y:
        return None

    return {
        "x": min_x,
        "y": min_y,
        "width": max_x - min_x + 1,
        "height": max_y - min_y + 1,
    }


def main():
    parser = argparse.ArgumentParser(description="Analyze cropped foreground-only scene exports.")
    parser.add_argument("--frames-dir", required=True)
    parser.add_argument("--output-json")
    parser.add_argument("--key-rgb", default="ff00ff", type=parse_rgb)
    args = parser.parse_args()

    frames_dir = Path(args.frames_dir)
    frame_paths = sorted([
        path for path in frames_dir.iterdir()
        if path.is_file() and path.suffix.lower() in {".bmp", ".png"}
    ])
    if not frame_paths:
        raise SystemExit(f"no frame images found in {frames_dir}")

    rows = []
    union_min_x = None
    union_min_y = None
    union_max_x = None
    union_max_y = None
    prev_rgba = None

    for frame_path in frame_paths:
        with Image.open(frame_path) as raw:
            rgb = raw.convert("RGB")
            rgba = raw.convert("RGBA")

        bbox = find_bbox(rgb, args.key_rgb)
        pixel_bytes, color_count = crop_pixels(rgba, bbox)
        indexed8_bytes = 0
        zlib_bytes = 0
        if bbox is not None:
            indexed8_bytes = bbox["width"] * bbox["height"]
            zlib_bytes = len(zlib.compress(pixel_bytes, level=9))
            if union_min_x is None or bbox["x"] < union_min_x:
                union_min_x = bbox["x"]
            if union_min_y is None or bbox["y"] < union_min_y:
                union_min_y = bbox["y"]
            x2 = bbox["x"] + bbox["width"] - 1
            y2 = bbox["y"] + bbox["height"] - 1
            if union_max_x is None or x2 > union_max_x:
                union_max_x = x2
            if union_max_y is None or y2 > union_max_y:
                union_max_y = y2

        delta = delta_bbox(prev_rgba, rgba)
        delta_indexed8_bytes = 0
        if delta is not None:
            delta_indexed8_bytes = delta["width"] * delta["height"]

        rows.append({
            "frame": frame_path.name,
            "bbox": bbox,
            "palette_colors": color_count,
            "indexed8_bytes": indexed8_bytes,
            "rgba_bytes": len(pixel_bytes),
            "zlib_rgba_bytes": zlib_bytes,
            "delta_bbox": delta,
            "delta_indexed8_bytes": delta_indexed8_bytes,
        })
        prev_rgba = rgba

    bbox_areas = [
        row["bbox"]["width"] * row["bbox"]["height"]
        for row in rows
        if row["bbox"] is not None
    ]
    delta_areas = [
        row["delta_bbox"]["width"] * row["delta_bbox"]["height"]
        for row in rows
        if row["delta_bbox"] is not None
    ]

    summary = {
        "frames_dir": str(frames_dir),
        "key_rgb": list(args.key_rgb),
        "frame_count": len(rows),
        "non_empty_frames": len(bbox_areas),
        "union_bbox": None if union_min_x is None else {
            "x": union_min_x,
            "y": union_min_y,
            "width": union_max_x - union_min_x + 1,
            "height": union_max_y - union_min_y + 1,
        },
        "total_indexed8_bytes": sum(row["indexed8_bytes"] for row in rows),
        "total_rgba_bytes": sum(row["rgba_bytes"] for row in rows),
        "total_zlib_rgba_bytes": sum(row["zlib_rgba_bytes"] for row in rows),
        "total_delta_indexed8_bytes": sum(row["delta_indexed8_bytes"] for row in rows),
        "bbox_area_stats": None if not bbox_areas else {
            "min": min(bbox_areas),
            "median": int(statistics.median(bbox_areas)),
            "max": max(bbox_areas),
        },
        "delta_area_stats": None if not delta_areas else {
            "min": min(delta_areas),
            "median": int(statistics.median(delta_areas)),
            "max": max(delta_areas),
        },
        "rows": rows,
    }

    payload = json.dumps(summary, indent=2) + "\n"
    if args.output_json:
        Path(args.output_json).write_text(payload, encoding="utf-8")
    else:
        print(payload, end="")


if __name__ == "__main__":
    main()
