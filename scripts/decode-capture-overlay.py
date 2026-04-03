#!/usr/bin/env python3
import argparse
import json
import sys
from itertools import permutations
from pathlib import Path
from zlib import crc32

from PIL import Image

FIXED_COLOR_MAP = {
    (0, 0, 0): 0,
    (0, 102, 255): 1,
    (0, 204, 102): 2,
    (255, 204, 0): 3,
}


def read_overlay_cells(image: Image.Image) -> list[tuple[int, int, int]]:
    img = image.convert("RGB")
    width, height = img.size
    width_cells = 32
    height_cells = 32
    cell_size = 4
    origin_x = width - width_cells * cell_size
    origin_y = height - height_cells * cell_size
    if origin_x < 0 or origin_y < 0:
        raise ValueError("image too small for overlay")

    cells = []
    for cell_y in range(height_cells):
        for cell_x in range(width_cells):
            if ((cell_x < 2 and cell_y < 2) or
                (cell_x >= width_cells - 2 and cell_y < 2) or
                (cell_x < 2 and cell_y >= height_cells - 2) or
                (cell_x >= width_cells - 2 and cell_y >= height_cells - 2)):
                continue
            x = origin_x + cell_x * cell_size + (cell_size // 2)
            y = origin_y + cell_y * cell_size + (cell_size // 2)
            cells.append(img.getpixel((x, y)))
    return cells


def unpack_packet(symbols: list[int]) -> bytes:
    packet = bytearray()
    value = 0
    count = 0
    for symbol in symbols:
        value |= (symbol & 0x3) << (count * 2)
        count += 1
        if count == 4:
            packet.append(value)
            value = 0
            count = 0
    if count:
        packet.append(value)
    return bytes(packet)


def decode_packet_from_cells(cells: list[tuple[int, int, int]]) -> dict:
    unique_colors = list(dict.fromkeys(cells))
    if len(unique_colors) > 4:
        raise ValueError("too many overlay colors")

    if all(color in FIXED_COLOR_MAP for color in unique_colors):
        symbols = [FIXED_COLOR_MAP[cell] for cell in cells]
        try:
            return parse_overlay(unpack_packet(symbols))
        except ValueError:
            pass

    for symbol_values in permutations(range(4), len(unique_colors)):
        color_map = dict(zip(unique_colors, symbol_values))
        symbols = [color_map[cell] for cell in cells]
        try:
            return parse_overlay(unpack_packet(symbols))
        except ValueError:
            continue

    raise ValueError("could not decode overlay payload")


def parse_overlay(packet: bytes) -> dict:
    if len(packet) < 6:
        raise ValueError("packet too short")
    payload_len = packet[0] | (packet[1] << 8)
    if len(packet) < payload_len + 6:
        raise ValueError("truncated packet")
    payload = packet[2:2 + payload_len]
    checksum = int.from_bytes(packet[2 + payload_len:2 + payload_len + 4], "little")
    if crc32(payload) & 0xFFFFFFFF != checksum:
        raise ValueError("overlay CRC mismatch")
    if payload[:4] != b"JCD1":
        raise ValueError("overlay magic mismatch")

    frame_number = int.from_bytes(payload[4:8], "little")
    total_draw_count = int.from_bytes(payload[8:10], "little")
    embedded_count = payload[10]
    offset = 11
    draws = []
    for _ in range(embedded_count):
        if offset + 12 > len(payload):
            raise ValueError("truncated draw list")
        x = int.from_bytes(payload[offset:offset + 2], "little", signed=False)
        if x >= 0x8000:
            x -= 0x10000
        y = int.from_bytes(payload[offset + 2:offset + 4], "little", signed=False)
        if y >= 0x8000:
            y -= 0x10000
        draws.append({
            "x": x,
            "y": y,
            "width": payload[offset + 4],
            "height": payload[offset + 5],
            "sprite_no": payload[offset + 6],
            "image_no": payload[offset + 7],
            "flipped": bool(payload[offset + 8]),
            "bmp_name_hash": (
                payload[offset + 9]
                | (payload[offset + 10] << 8)
                | (payload[offset + 11] << 16)
            ),
        })
        offset += 12

    return {
        "frame_number": frame_number,
        "draw_count": total_draw_count,
        "embedded_draw_count": embedded_count,
        "draws": draws,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Decode host capture overlay from a BMP/PNG frame")
    parser.add_argument("image", help="Path to frame image")
    parser.add_argument("--json", action="store_true", help="Emit JSON")
    args = parser.parse_args()

    image_path = Path(args.image)
    with Image.open(image_path) as image:
        decoded = decode_packet_from_cells(read_overlay_cells(image))

    if args.json:
        json.dump(decoded, sys.stdout, indent=2)
        sys.stdout.write("\n")
    else:
        print(f"frame={decoded['frame_number']} draw_count={decoded['draw_count']} embedded={decoded['embedded_draw_count']}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
