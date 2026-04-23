#!/usr/bin/env python3

import argparse
import json
import struct
from pathlib import Path


def load_frame_paths(frame_meta_dir: Path) -> list[Path]:
    frame_paths = sorted(frame_meta_dir.glob("frame_*.json"))
    if not frame_paths:
        raise SystemExit(f"no frame metadata found in {frame_meta_dir}")
    return frame_paths


def clip_name(name: str) -> bytes:
    encoded = name.encode("ascii", "replace")[:19]
    return encoded + b"\0" * (20 - len(encoded))


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a PS1 foreground backdrop draw pack.")
    parser.add_argument("--frame-meta-dir", required=True)
    parser.add_argument("--output-pack", required=True)
    parser.add_argument("--output-json")
    parser.add_argument("--scene-label", default="")
    parser.add_argument("--surface-role", default="background")
    parser.add_argument("--bmp-name", action="append", default=[],
                        help="Restrict to one or more bmp names")
    parser.add_argument("--reduce-to-state", action="store_true",
                        help="Treat visible_draws as cumulative history and emit only the current state per frame")
    args = parser.parse_args()

    frame_meta_dir = Path(args.frame_meta_dir)
    frame_paths = load_frame_paths(frame_meta_dir)

    name_to_index: dict[str, int] = {}
    names: list[str] = []
    frame_rows: list[tuple[int, int]] = []
    draw_rows: list[tuple[int, int, int, int, int, int]] = []
    bmp_filter = set(args.bmp_name)

    for meta_path in frame_paths:
        payload = json.loads(meta_path.read_text(encoding="utf-8"))
        draws = payload.get("visible_draws", [])
        first_draw = len(draw_rows)
        frame_draws = []

        for draw in draws:
            if draw.get("surface_role") != args.surface_role:
                continue

            bmp_name = draw.get("bmp_name", "")
            if not bmp_name:
                continue
            if bmp_filter and bmp_name not in bmp_filter:
                continue

            frame_draws.append(draw)

        if args.reduce_to_state:
            latest_by_key: dict[tuple[object, ...], tuple[int, dict]] = {}
            for order, draw in enumerate(frame_draws):
                key = (
                    draw.get("bmp_name", ""),
                    int(draw.get("image_no", 0) or 0),
                    int(draw.get("x", 0) or 0),
                    int(draw.get("y", 0) or 0),
                    int(draw.get("width", 0) or 0),
                    int(draw.get("height", 0) or 0),
                    1 if draw.get("flipped", False) else 0,
                )
                latest_by_key[key] = (order, draw)
            frame_draws = [draw for _, draw in sorted(latest_by_key.values(), key=lambda item: item[0])]

        for draw in frame_draws:
            bmp_name = draw.get("bmp_name", "")

            if bmp_name not in name_to_index:
                name_to_index[bmp_name] = len(names)
                names.append(bmp_name)

            draw_rows.append((
                name_to_index[bmp_name],
                int(draw.get("image_no", 0) or 0),
                int(draw.get("sprite_no", 0) or 0),
                int(draw.get("x", 0) or 0),
                int(draw.get("y", 0) or 0),
                1 if draw.get("flipped", False) else 0,
            ))

        frame_rows.append((first_draw, len(draw_rows) - first_draw))

    frame_table_offset = 24
    name_table_offset = frame_table_offset + (len(frame_rows) * 4)
    draw_table_offset = name_table_offset + (len(names) * 20)

    header = struct.pack(
        "<4sHHHHIII",
        b"FOC1",
        1,
        len(frame_rows),
        len(names),
        len(draw_rows),
        frame_table_offset,
        name_table_offset,
        draw_table_offset,
    )

    output_pack = Path(args.output_pack)
    output_pack.parent.mkdir(parents=True, exist_ok=True)

    with output_pack.open("wb") as f:
        f.write(header)
        for first_draw, draw_count in frame_rows:
            f.write(struct.pack("<HH", first_draw, draw_count))
        for name in names:
            f.write(clip_name(name))
        for name_index, image_no, sprite_no, x, y, flipped in draw_rows:
            f.write(struct.pack(
                "<BBHhhBB",
                name_index,
                image_no,
                sprite_no,
                x,
                y,
                flipped,
                0,
            ))

    summary = {
        "scene_label": args.scene_label,
        "surface_role": args.surface_role,
        "bmp_filter": sorted(bmp_filter),
        "reduce_to_state": args.reduce_to_state,
        "frame_count": len(frame_rows),
        "name_count": len(names),
        "draw_count": len(draw_rows),
        "output_pack": str(output_pack),
        "names": names,
        "frames": [
            {"frame": idx, "first_draw": first_draw, "draw_count": draw_count}
            for idx, (first_draw, draw_count) in enumerate(frame_rows)
        ],
        "pack_size_bytes": output_pack.stat().st_size,
    }

    payload = json.dumps(summary, indent=2) + "\n"
    if args.output_json:
        Path(args.output_json).write_text(payload, encoding="utf-8")
    else:
        print(payload, end="")


if __name__ == "__main__":
    main()
