#!/usr/bin/env python3
"""Find empirical scene cutoffs and non-terminal pause plateaus in captured frames."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image


def pixel_hash(path: Path) -> str:
    with Image.open(path) as img:
        return hashlib.sha256(img.convert("RGBA").tobytes()).hexdigest()


def build_runs(frames: list[tuple[int, str]]) -> list[dict]:
    runs: list[dict] = []
    start_frame, current_hash = frames[0]
    length = 1
    for frame_no, frame_hash in frames[1:]:
        if frame_hash == current_hash:
            length += 1
            continue
        runs.append(
            {
                "start_frame": start_frame,
                "end_frame": frame_no - 1,
                "length": length,
                "hash": current_hash,
            }
        )
        start_frame = frame_no
        current_hash = frame_hash
        length = 1
    runs.append(
        {
            "start_frame": start_frame,
            "end_frame": frames[-1][0],
            "length": length,
            "hash": current_hash,
        }
    )
    return runs


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("frames_dir", type=Path, help="Directory containing frame_*.png or frame_*.bmp")
    ap.add_argument(
        "--pause-threshold",
        type=int,
        default=2,
        help="Minimum identical-frame run to report as a pause candidate (default: 2)",
    )
    ap.add_argument(
        "--tail-min-length",
        type=int,
        default=2,
        help="Minimum identical-frame tail run to accept as stable tail (default: 2)",
    )
    return ap


def main() -> int:
    args = build_parser().parse_args()
    frames_dir = args.frames_dir

    pngs = sorted(frames_dir.glob("**/frame_*.png"))
    bmps = sorted(frames_dir.glob("**/frame_*.bmp"))
    frames = pngs if pngs else bmps
    if not frames:
        raise SystemExit(f"no captured frames found in {frames_dir}")

    hashed_frames: list[tuple[int, str]] = []
    for path in frames:
        frame_no = int(path.stem.split("_")[1])
        hashed_frames.append((frame_no, pixel_hash(path)))

    changes: list[int] = []
    for idx in range(1, len(hashed_frames)):
        if hashed_frames[idx][1] != hashed_frames[idx - 1][1]:
            changes.append(hashed_frames[idx][0])

    runs = build_runs(hashed_frames)
    stable_tail_start = None
    stable_tail_length = None
    if runs and runs[-1]["length"] >= args.tail_min_length:
        stable_tail_start = runs[-1]["start_frame"]
        stable_tail_length = runs[-1]["length"]

    pauses = []
    for run in runs[:-1]:
        if run["length"] >= args.pause_threshold:
            pauses.append(
                {
                    "start_frame": run["start_frame"],
                    "end_frame": run["end_frame"],
                    "length": run["length"],
                }
            )

    payload = {
        "frames_dir": str(frames_dir.resolve()),
        "frame_count": len(hashed_frames),
        "first_frame": hashed_frames[0][0],
        "last_frame": hashed_frames[-1][0],
        "first_change_frame": changes[0] if changes else None,
        "last_change_frame": changes[-1] if changes else None,
        "stable_tail_start_frame": stable_tail_start,
        "stable_tail_length": stable_tail_length,
        "pause_count": len(pauses),
        "pauses": pauses,
        "longest_pause": max(pauses, key=lambda row: row["length"]) if pauses else None,
    }
    print(json.dumps(payload, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
