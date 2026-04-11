#!/usr/bin/env python3
import argparse
import hashlib
import json
import re
import sys
from pathlib import Path


FRAME_RE = re.compile(r"frame_(\d+)\.(?:png|bmp)$", re.IGNORECASE)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Validate a dense FISHING 1 PS1 result bundle against labeled full-scene ground truth."
    )
    parser.add_argument("--annotations", required=True, help="Path to full-scene annotations.json")
    parser.add_argument("--result", required=True, help="Path to candidate result.json")
    parser.add_argument("--capture-lead", type=int, default=180,
                        help="Frames of dense capture to keep before the first sustained correct run")
    parser.add_argument("--review-lead", type=int, default=120,
                        help="Frames of review window to keep before the first sustained correct run")
    parser.add_argument("--min-unique-review-frames", type=int, default=6,
                        help="Minimum number of unique frame hashes in the derived review window")
    return parser.parse_args()


def frame_no_from_path(path_text):
    match = FRAME_RE.search(path_text or "")
    if not match:
        return None
    return int(match.group(1))


def load_annotations(path):
    payload = json.loads(Path(path).read_text(encoding="utf-8"))
    rows = payload.get("frames") if isinstance(payload, dict) else payload
    if not isinstance(rows, list):
        raise ValueError("annotations payload does not contain a frame list")
    return rows


def derive_ground_truth(rows, capture_lead, review_lead):
    labeled = []
    shoe_only = []
    for row in rows:
        frame_no = frame_no_from_path(row.get("query_image") or row.get("query_rel") or row.get("image_path", ""))
        if frame_no is None:
            continue
        labels = row.get("labels") or {}
        correct = bool(labels.get("correct"))
        johnny = bool(labels.get("johnny_visible"))
        other = bool(labels.get("other_sprites_visible"))
        if correct and johnny:
            labeled.append(frame_no)
        if other and not johnny:
            shoe_only.append(frame_no)

    if not labeled:
        raise ValueError("no labeled correct Johnny frames found")

    runs = []
    run_start = run_prev = labeled[0]
    for frame_no in labeled[1:]:
        if frame_no == run_prev + 30:
            run_prev = frame_no
            continue
        runs.append((run_start, run_prev))
        run_start = run_prev = frame_no
    runs.append((run_start, run_prev))

    first_best_run = max(runs, key=lambda pair: (pair[1] - pair[0], -pair[0]))
    correct_start, correct_end = first_best_run
    first_shoe_only = min(shoe_only) if shoe_only else None

    return {
        "correct_run_start": correct_start,
        "correct_run_end": correct_end,
        "first_shoe_only_frame": first_shoe_only,
        "recommended_capture_start": max(0, correct_start - capture_lead),
        "recommended_review_start": max(0, correct_start - review_lead),
        "recommended_review_end": correct_end,
    }


def load_result(path):
    payload = json.loads(Path(path).read_text(encoding="utf-8"))
    frames_dir = Path((payload.get("paths") or {}).get("frames_dir") or "")
    if not frames_dir.is_dir():
        raise ValueError(f"frames_dir missing: {frames_dir}")
    return payload, frames_dir


def frame_hashes(frames_dir):
    hashes = {}
    for frame_path in sorted(frames_dir.rglob("frame_*.png")):
        frame_no = frame_no_from_path(frame_path.name)
        if frame_no is None:
            continue
        hashes[frame_no] = hashlib.sha256(frame_path.read_bytes()).hexdigest()
    return hashes


def main():
    args = parse_args()
    truth = derive_ground_truth(load_annotations(args.annotations), args.capture_lead, args.review_lead)
    result, frames_dir = load_result(args.result)
    hashes = frame_hashes(frames_dir)
    config = result.get("config") or {}
    scene = result.get("scene") or {}

    issues = []

    if int(config.get("start_frame", -1)) != truth["recommended_capture_start"]:
        issues.append(
            f"capture_start {config.get('start_frame')} != recommended {truth['recommended_capture_start']}"
        )
    if int(config.get("interval", -1)) != 10:
        issues.append(f"interval {config.get('interval')} != 10")
    if scene.get("boot_string") != "story scene 17 seed 1":
        issues.append(f"boot_string {scene.get('boot_string')!r} != 'story scene 17 seed 1'")

    required = [
        truth["recommended_review_start"],
        truth["correct_run_start"],
        truth["correct_run_end"],
    ]
    for frame_no in required:
        if frame_no not in hashes:
            issues.append(f"missing frame_{frame_no:05d}.png")

    review_hashes = {
        hashes[frame_no]
        for frame_no in sorted(hashes)
        if truth["recommended_review_start"] <= frame_no <= truth["recommended_review_end"]
    }
    if len(review_hashes) < args.min_unique_review_frames:
        issues.append(
            f"review window has only {len(review_hashes)} unique frames "
            f"(need >= {args.min_unique_review_frames})"
        )

    if truth["first_shoe_only_frame"] is not None:
        if truth["recommended_review_end"] >= truth["first_shoe_only_frame"]:
            issues.append(
                f"review_end {truth['recommended_review_end']} reaches shoe-only frame "
                f"{truth['first_shoe_only_frame']}"
            )

    report = {
        "ok": not issues,
        "issues": issues,
        "truth": truth,
        "result": {
            "start_frame": config.get("start_frame"),
            "frames": config.get("frames"),
            "interval": config.get("interval"),
            "frames_dir": str(frames_dir),
            "unique_review_frames": len(review_hashes),
        },
    }
    json.dump(report, sys.stdout, indent=2)
    sys.stdout.write("\n")
    raise SystemExit(0 if not issues else 1)


if __name__ == "__main__":
    main()
