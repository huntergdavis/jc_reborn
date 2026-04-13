#!/usr/bin/env python3

import argparse
from pathlib import Path

from PIL import Image


def rgb888_to_ps1(rgb: tuple[int, int, int]) -> int:
    r, g, b = rgb
    value = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)
    return 0x8000 if value == 0 else value


def main() -> None:
    parser = argparse.ArgumentParser(description="Convert a 640x480 host frame into PS1 15-bit RAW.")
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)

    with Image.open(input_path) as raw:
        rgb = raw.convert("RGB")

    if rgb.size != (640, 480):
        raise SystemExit(f"expected 640x480 image, got {rgb.size}")

    pixels = rgb.load()
    out = bytearray(640 * 480 * 2)
    offset = 0
    for y in range(480):
        for x in range(640):
            value = rgb888_to_ps1(pixels[x, y])
            out[offset + 0] = value & 0xFF
            out[offset + 1] = (value >> 8) & 0xFF
            offset += 2

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(out)


if __name__ == "__main__":
    main()
