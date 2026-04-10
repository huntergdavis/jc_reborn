#!/usr/bin/env python3
import argparse
import hashlib
import json
import re
from pathlib import Path

from PIL import Image


FRAME_RE = re.compile(r"frame_(\d+)\.(?:png|bmp)$", re.IGNORECASE)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Summarize a full FISHING 1 result bundle against labeled full-scene ground truth."
    )
    parser.add_argument("--annotations", required=True, help="Path to fishing full-scene annotations.json")
    parser.add_argument("--result", required=True, help="Path to result.json or result directory")
    return parser.parse_args()


def frame_no_from_text(text):
    match = FRAME_RE.search(text or "")
    if not match:
        return None
    return int(match.group(1))


def load_annotations(path):
    payload = json.loads(Path(path).read_text(encoding="utf-8"))
    rows = payload.get("frames") if isinstance(payload, dict) else payload
    labeled = []
    shoe_only = []
    black_frames = []
    ocean_only = []
    island_only = []
    for row in rows:
        frame_no = frame_no_from_text(row.get("query_image") or row.get("query_rel") or row.get("image_path", ""))
        if frame_no is None:
            continue
        labels = row.get("labels") or {}
        if bool(labels.get("correct")) and bool(labels.get("johnny_visible")):
            labeled.append(frame_no)
        if bool(labels.get("other_sprites_visible")) and not bool(labels.get("johnny_visible")):
            shoe_only.append(frame_no)
        if bool(labels.get("black_screen")):
            black_frames.append(frame_no)
        if bool(labels.get("only_ocean")):
            ocean_only.append(frame_no)
        if bool(labels.get("only_island")):
            island_only.append(frame_no)
    labeled.sort()
    shoe_only.sort()
    black_frames.sort()
    ocean_only.sort()
    island_only.sort()
    runs = []
    start = prev = labeled[0]
    for frame_no in labeled[1:]:
        if frame_no == prev + 30:
            prev = frame_no
            continue
        runs.append((start, prev))
        start = prev = frame_no
    runs.append((start, prev))
    best_run = max(runs, key=lambda pair: (pair[1] - pair[0], -pair[0]))
    return {
        "correct_run_start": best_run[0],
        "correct_run_end": best_run[1],
        "correct_frames": labeled,
        "shoe_only_frames": shoe_only,
        "black_frames": black_frames,
        "ocean_only_frames": ocean_only,
        "island_only_frames": island_only,
    }


def load_result(path):
    path = Path(path)
    if path.is_dir():
        path = path / "result.json"
    payload = json.loads(path.read_text(encoding="utf-8"))
    frames_dir = Path((payload.get("paths") or {}).get("frames_dir") or "")
    return payload, frames_dir


def collect_hashes(frames_dir):
    hashes = {}
    for frame_path in sorted(frames_dir.rglob("frame_*.png")):
        frame_no = frame_no_from_text(frame_path.name)
        if frame_no is None:
            continue
        hashes[frame_no] = hashlib.sha256(frame_path.read_bytes()).hexdigest()
    return hashes


def collect_nonblack_frames(frames_dir):
    rows = {}
    for frame_path in sorted(frames_dir.rglob("frame_*.png")):
        frame_no = frame_no_from_text(frame_path.name)
        if frame_no is None:
            continue
        with Image.open(frame_path) as image:
            bbox = image.convert("RGB").getbbox()
        visible_height = 0
        lower_half_visible = False
        full_height_visible = False
        if bbox is not None:
            visible_height = max(0, bbox[3] - bbox[1])
            lower_half_visible = bbox[3] > 240
            full_height_visible = bbox[0] == 0 and bbox[1] == 0 and bbox[2] == 640 and bbox[3] == 448
        rows[frame_no] = {
            "nonblack": bbox is not None,
            "bbox": list(bbox) if bbox is not None else None,
            "visible_height": visible_height,
            "lower_half_visible": lower_half_visible,
            "full_height_visible": full_height_visible,
        }
    return rows


def phase_rows(frame_nos, hashes):
    rows = []
    for frame_no in frame_nos:
        digest = hashes.get(frame_no)
        if digest is None:
            continue
        rows.append(
            {
                "frame": frame_no,
                "sha256": digest,
                "short_hash": digest[:16],
            }
        )
    return rows


def main():
    args = parse_args()
    truth = load_annotations(args.annotations)
    payload, frames_dir = load_result(args.result)
    hashes = collect_hashes(frames_dir)
    nonblack = collect_nonblack_frames(frames_dir)

    correct_present = [f for f in truth["correct_frames"] if f in hashes]
    shoe_present = [f for f in truth["shoe_only_frames"] if f in hashes]
    black_present = [f for f in truth["black_frames"] if f in hashes]
    ocean_present = [f for f in truth["ocean_only_frames"] if f in hashes]
    island_present = [f for f in truth["island_only_frames"] if f in hashes]
    correct_hashes = {hashes[f] for f in correct_present}
    shoe_hashes = {hashes[f] for f in shoe_present}
    black_hashes = {hashes[f] for f in black_present}
    ocean_hashes = {hashes[f] for f in ocean_present}
    island_hashes = {hashes[f] for f in island_present}
    first_shoe_only = truth["shoe_only_frames"][0] if truth["shoe_only_frames"] else None
    post_correct_pre_shoe_present = []
    if first_shoe_only is not None:
        post_correct_pre_shoe_present = [
            frame_no
            for frame_no in sorted(hashes)
            if truth["correct_run_end"] < frame_no < first_shoe_only
        ]
    post_correct_pre_shoe_hashes = {hashes[f] for f in post_correct_pre_shoe_present}
    visible_frames = [frame_no for frame_no in sorted(nonblack) if nonblack[frame_no]["nonblack"]]
    lower_half_visible_frames = [
        frame_no for frame_no in sorted(nonblack) if nonblack[frame_no]["lower_half_visible"]
    ]
    full_height_visible_frames = [
        frame_no for frame_no in sorted(nonblack) if nonblack[frame_no]["full_height_visible"]
    ]
    partial_height_visible_frames = [
        frame_no
        for frame_no in sorted(nonblack)
        if nonblack[frame_no]["nonblack"] and not nonblack[frame_no]["full_height_visible"]
    ]
    first_visible_frame = visible_frames[0] if visible_frames else None
    first_lower_half_visible_frame = lower_half_visible_frames[0] if lower_half_visible_frames else None
    first_full_height_visible_frame = full_height_visible_frames[0] if full_height_visible_frames else None
    last_partial_height_visible_frame = (
        partial_height_visible_frames[-1] if partial_height_visible_frames else None
    )
    last_black_frame = max((frame_no for frame_no, row in nonblack.items() if not row["nonblack"]), default=None)

    if first_visible_frame is None:
        startup_regime = "never_visible"
    elif first_full_height_visible_frame == first_visible_frame:
        startup_regime = "full_height_visible_immediately"
    elif first_lower_half_visible_frame == first_visible_frame:
        startup_regime = "boxed_or_partial_full_scene_startup"
    else:
        startup_regime = "top_only_then_full_height_startup"

    if not correct_present:
        regime = "cut_off_before_correct_window"
    elif len(correct_hashes) == 1:
        regime = "collapsed_in_correct_window"
    else:
        regime = "reaches_labeled_correct_window"

    summary = {
        "regime": regime,
        "startup_regime": startup_regime,
        "result": {
            "start_frame": (payload.get("config") or {}).get("start_frame"),
            "frames": (payload.get("config") or {}).get("frames"),
            "interval": (payload.get("config") or {}).get("interval"),
            "frames_captured": (payload.get("outcome") or {}).get("frames_captured"),
            "state_hash": (payload.get("outcome") or {}).get("state_hash"),
            "frames_dir": str(frames_dir),
        },
        "truth": {
            "correct_run_start": truth["correct_run_start"],
            "correct_run_end": truth["correct_run_end"],
            "first_shoe_only_frame": first_shoe_only,
            "first_black_frame": truth["black_frames"][0] if truth["black_frames"] else None,
            "first_ocean_only_frame": truth["ocean_only_frames"][0] if truth["ocean_only_frames"] else None,
            "first_island_only_frame": truth["island_only_frames"][0] if truth["island_only_frames"] else None,
            "first_visible_target_frame": truth["ocean_only_frames"][0] if truth["ocean_only_frames"] else None,
        },
        "coverage": {
            "black_frames_present": black_present,
            "ocean_only_frames_present": ocean_present,
            "island_only_frames_present": island_present,
            "correct_frames_present": correct_present,
            "shoe_only_frames_present": shoe_present,
            "post_correct_pre_shoe_frames_present": post_correct_pre_shoe_present,
            "unique_black_hashes": len(black_hashes),
            "unique_ocean_only_hashes": len(ocean_hashes),
            "unique_island_only_hashes": len(island_hashes),
            "unique_correct_hashes": len(correct_hashes),
            "unique_shoe_hashes": len(shoe_hashes),
            "unique_post_correct_pre_shoe_hashes": len(post_correct_pre_shoe_hashes),
            "first_visible_frame": first_visible_frame,
            "first_lower_half_visible_frame": first_lower_half_visible_frame,
            "first_full_height_visible_frame": first_full_height_visible_frame,
            "last_partial_height_visible_frame": last_partial_height_visible_frame,
            "last_black_frame": last_black_frame,
        },
        "phase_hashes": {
            "black": phase_rows(black_present, hashes),
            "ocean_only": phase_rows(ocean_present, hashes),
            "island_only": phase_rows(island_present, hashes),
            "correct": phase_rows(correct_present, hashes),
            "shoe_only": phase_rows(shoe_present, hashes),
            "post_correct_pre_shoe": phase_rows(post_correct_pre_shoe_present, hashes),
        },
    }
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
