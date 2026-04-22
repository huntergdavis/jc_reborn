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


def parse_augment_bounds(value: str) -> tuple[int, int, int, int]:
    parts = [p.strip() for p in value.split(",")]
    if len(parts) != 4:
        raise argparse.ArgumentTypeError("expected x_min,y_min,x_max,y_max")
    return tuple(int(p) for p in parts)  # type: ignore[return-value]


def parse_frame_range(value: str) -> tuple[int, int]:
    parts = [p.strip() for p in value.split(":")]
    if len(parts) != 2:
        raise argparse.ArgumentTypeError("expected start:end (inclusive)")
    start = int(parts[0])
    end = int(parts[1])
    if end < start:
        raise argparse.ArgumentTypeError("end must be >= start")
    return (start, end)


def collect_fg_palette(frame_paths, key_rgb):
    palette = set()
    for path in frame_paths:
        with Image.open(path) as raw:
            rgb = raw.convert("RGB")
        for _, col in (rgb.getcolors(maxcolors=1 << 24) or []):
            if col != key_rgb:
                palette.add(col)
    return palette


def augment_fg(fg_rgb, full_rgb, base_rgb, fg_palette, key_rgb, bounds):
    if full_rgb is None or base_rgb is None:
        return fg_rgb
    if full_rgb.size != fg_rgb.size or base_rgb.size != fg_rgb.size:
        return fg_rgb
    out = fg_rgb.copy()
    op = out.load()
    fp = fg_rgb.load()
    ffp = full_rgb.load()
    bp = base_rgb.load()
    w, h = fg_rgb.size
    if bounds is None:
        x_min, y_min, x_max, y_max = 0, 0, w - 1, h - 1
    else:
        x_min, y_min, x_max, y_max = bounds
        x_min = max(0, x_min)
        y_min = max(0, y_min)
        x_max = min(w - 1, x_max)
        y_max = min(h - 1, y_max)
    for y in range(y_min, y_max + 1):
        for x in range(x_min, x_max + 1):
            if fp[x, y] != key_rgb:
                continue
            pf = ffp[x, y]
            if pf not in fg_palette:
                continue
            if pf == bp[x, y]:
                continue
            op[x, y] = pf
    return out


def main():
    parser = argparse.ArgumentParser(description="Analyze cropped foreground-only scene exports.")
    parser.add_argument("--frames-dir", required=True)
    parser.add_argument("--output-json")
    parser.add_argument("--key-rgb", default="ff00ff", type=parse_rgb)
    parser.add_argument(
        "--full-frames-dir",
        help="Directory of full-render frames, same seed / indices as --frames-dir.",
    )
    parser.add_argument(
        "--scene-base-frame",
        type=int,
        default=0,
        help="Index into --full-frames-dir to treat as pristine scene base (default 0).",
    )
    parser.add_argument(
        "--augment-bounds",
        type=parse_augment_bounds,
        help="Optional x_min,y_min,x_max,y_max restricting scene-base augmentation.",
    )
    parser.add_argument(
        "--augment-frame-range",
        type=parse_frame_range,
        help="Inclusive start:end frame-index range to augment; frames outside are "
             "left as pure foreground-only captures.",
    )
    args = parser.parse_args()

    frames_dir = Path(args.frames_dir)
    frame_paths = sorted([
        path for path in frames_dir.iterdir()
        if path.is_file() and path.suffix.lower() in {".bmp", ".png"}
    ])
    if not frame_paths:
        raise SystemExit(f"no frame images found in {frames_dir}")

    full_frame_paths = []
    base_img = None
    fg_palette = set()
    if args.full_frames_dir:
        full_dir = Path(args.full_frames_dir)
        if not full_dir.is_dir():
            raise SystemExit(f"--full-frames-dir not found: {full_dir}")
        full_frame_paths = sorted(
            path for path in full_dir.iterdir()
            if path.is_file() and path.suffix.lower() in {".bmp", ".png"}
        )
        if len(full_frame_paths) != len(frame_paths):
            raise SystemExit(
                f"full-frames-dir count {len(full_frame_paths)} != frames-dir count {len(frame_paths)}"
            )
        if args.scene_base_frame < 0 or args.scene_base_frame >= len(full_frame_paths):
            raise SystemExit(
                f"--scene-base-frame {args.scene_base_frame} out of range"
            )
        with Image.open(full_frame_paths[args.scene_base_frame]) as raw:
            base_img = raw.convert("RGB")
        fg_palette = collect_fg_palette(frame_paths, args.key_rgb)

    rows = []
    union_min_x = None
    union_min_y = None
    union_max_x = None
    union_max_y = None
    prev_rgba = None

    for frame_index, frame_path in enumerate(frame_paths):
        with Image.open(frame_path) as raw:
            rgb = raw.convert("RGB")
            rgba = raw.convert("RGBA")

        in_augment_range = True
        if args.augment_frame_range is not None:
            lo, hi = args.augment_frame_range
            in_augment_range = (lo <= frame_index <= hi)

        if full_frame_paths and base_img is not None and in_augment_range:
            with Image.open(full_frame_paths[frame_index]) as raw_full:
                full_rgb = raw_full.convert("RGB")
            rgb = augment_fg(rgb, full_rgb, base_img, fg_palette, args.key_rgb, args.augment_bounds)
            rgba = rgb.convert("RGBA")

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
        "full_frames_dir": str(Path(args.full_frames_dir).resolve()) if args.full_frames_dir else None,
        "scene_base_frame": args.scene_base_frame if full_frame_paths else None,
        "augment_bounds": list(args.augment_bounds) if args.augment_bounds else None,
        "fg_palette_size": len(fg_palette) if fg_palette else None,
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
