#!/usr/bin/env python3

import argparse
import json
import struct
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


def rgb888_to_ps1(rgb: tuple[int, int, int]) -> int:
    r, g, b = rgb
    value = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)
    if value == 0:
        value = 0x8000
    return value


def encode_crop(img: Image.Image, bbox, key_rgb: tuple[int, int, int]) -> bytes:
    if bbox is None:
        return b""

    crop = img.crop((
        bbox["x"],
        bbox["y"],
        bbox["x"] + bbox["width"],
        bbox["y"] + bbox["height"],
    )).convert("RGB")
    pixels = crop.load()
    width, height = crop.size
    out = bytearray(width * height * 2)
    i = 0

    for y in range(height):
        for x in range(width):
            rgb = pixels[x, y]
            if rgb == key_rgb:
                value = 0
            else:
                value = rgb888_to_ps1(rgb)
            out[i:i + 2] = struct.pack("<H", value)
            i += 2

    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description="Build a PS1 foreground playback pack for FISHING 1.")
    parser.add_argument("--frames-dir", required=True)
    parser.add_argument("--output-pack", required=True)
    parser.add_argument("--output-json")
    parser.add_argument("--key-rgb", default="ff00ff", type=parse_rgb)
    parser.add_argument("--frame-step", type=int, default=6)
    args = parser.parse_args()

    frames_dir = Path(args.frames_dir)
    frame_paths = sorted(
        path for path in frames_dir.iterdir()
        if path.is_file() and path.suffix.lower() in {".bmp", ".png"}
    )
    if not frame_paths:
        raise SystemExit(f"no frame images found in {frames_dir}")
    if args.frame_step <= 0:
        raise SystemExit("--frame-step must be > 0")

    selected_indices = list(range(0, len(frame_paths), args.frame_step))
    if selected_indices[-1] != (len(frame_paths) - 1):
        selected_indices.append(len(frame_paths) - 1)

    rows = []
    data_chunks: list[bytes] = []
    union_min_x = None
    union_min_y = None
    union_max_x = None
    union_max_y = None

    table_offset = 32
    data_offset = table_offset + (len(selected_indices) * 20)
    next_offset = data_offset

    for source_index in selected_indices:
        frame_path = frame_paths[source_index]
        with Image.open(frame_path) as raw:
            rgb = raw.convert("RGB")

        bbox = find_bbox(rgb, args.key_rgb)
        payload = encode_crop(rgb, bbox, args.key_rgb)

        if bbox is not None:
            x2 = bbox["x"] + bbox["width"] - 1
            y2 = bbox["y"] + bbox["height"] - 1
            if union_min_x is None or bbox["x"] < union_min_x:
                union_min_x = bbox["x"]
            if union_min_y is None or bbox["y"] < union_min_y:
                union_min_y = bbox["y"]
            if union_max_x is None or x2 > union_max_x:
                union_max_x = x2
            if union_max_y is None or y2 > union_max_y:
                union_max_y = y2

        rows.append({
            "source_frame": source_index,
            "frame": frame_path.name,
            "x": 0 if bbox is None else bbox["x"],
            "y": 0 if bbox is None else bbox["y"],
            "width": 0 if bbox is None else bbox["width"],
            "height": 0 if bbox is None else bbox["height"],
            "data_offset": next_offset,
            "data_size": len(payload),
        })
        data_chunks.append(payload)
        next_offset += len(payload)

    header = struct.pack(
        "<4sHHHHHHHHHHII",
        b"FGP1",
        1,
        len(rows),
        args.frame_step,
        0,
        640,
        480,
        0 if union_min_x is None else union_min_x,
        0 if union_min_y is None else union_min_y,
        0 if union_min_x is None else (union_max_x - union_min_x + 1),
        0 if union_min_y is None else (union_max_y - union_min_y + 1),
        table_offset,
        data_offset,
    )

    output_pack = Path(args.output_pack)
    output_pack.parent.mkdir(parents=True, exist_ok=True)

    with output_pack.open("wb") as f:
        f.write(header)
        for row in rows:
            f.write(struct.pack(
                "<HHHHHHII",
                row["source_frame"],
                row["x"],
                row["y"],
                row["width"],
                row["height"],
                0,
                row["data_offset"],
                row["data_size"],
            ))
        for chunk in data_chunks:
            f.write(chunk)

    summary = {
        "frames_dir": str(frames_dir),
        "output_pack": str(output_pack),
        "frame_step": args.frame_step,
        "pack_frame_count": len(rows),
        "source_frame_count": len(frame_paths),
        "union_bbox": None if union_min_x is None else {
            "x": union_min_x,
            "y": union_min_y,
            "width": union_max_x - union_min_x + 1,
            "height": union_max_y - union_min_y + 1,
        },
        "total_payload_bytes": sum(row["data_size"] for row in rows),
        "pack_size_bytes": output_pack.stat().st_size,
        "rows": rows,
    }

    payload = json.dumps(summary, indent=2) + "\n"
    if args.output_json:
        Path(args.output_json).write_text(payload, encoding="utf-8")
    else:
        print(payload, end="")


if __name__ == "__main__":
    main()
