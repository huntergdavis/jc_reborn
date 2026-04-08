#!/usr/bin/env python3
"""
Reference-first visual classifier for JC regtest scenes.

This tool is intentionally CPU-only and depends only on Pillow and NumPy.
It builds a reusable feature bank from canonical reference scenes and can
analyze new runs against that bank.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image
from PIL import UnidentifiedImageError


THUMB_W = 32
THUMB_H = 24
ANALYSIS_W = 96
ANALYSIS_H = 72
MEDIAN_SAMPLE_CAP = 48


def find_project_root() -> Path:
    here = Path(__file__).resolve().parent
    root = here.parent
    if (root / "config" / "ps1" / "regtest-scenes.txt").is_file():
        return root
    return Path.cwd()


def scene_family(scene_id: str) -> str:
    return scene_id.split("-", 1)[0]


def frame_number(frame_path: Path) -> int:
    stem = frame_path.stem
    try:
        return int(stem.split("_")[-1])
    except Exception:
        return -1


def image_to_rgb(path: Path) -> np.ndarray:
    with Image.open(path) as img:
        rgb = img.convert("RGB").resize((ANALYSIS_W, ANALYSIS_H), Image.Resampling.BILINEAR)
        return np.array(rgb, dtype=np.uint8)


def safe_image_to_rgb(path: Path) -> np.ndarray | None:
    try:
        return image_to_rgb(path)
    except (OSError, UnidentifiedImageError, ValueError):
        return None


def resize_gray(rgb: np.ndarray, width: int = THUMB_W, height: int = THUMB_H) -> np.ndarray:
    img = Image.fromarray(rgb, mode="RGB").convert("L").resize((width, height), Image.Resampling.BILINEAR)
    arr = np.array(img, dtype=np.float32) / 255.0
    return arr


def sample_frame_paths(scene_dir: Path) -> list[Path]:
    frames_dir = scene_dir / "frames"
    source_dir = frames_dir if frames_dir.is_dir() else scene_dir
    if not source_dir.is_dir():
        return []
    frames = sorted(
        [p for p in source_dir.glob("**/*") if p.is_file() and p.suffix.lower() in {".bmp", ".png", ".jpg", ".jpeg"}],
        key=frame_number,
    )
    return frames


def evenly_sample(items: list[Path], cap: int) -> list[Path]:
    if len(items) <= cap:
        return items
    idxs = np.linspace(0, len(items) - 1, num=cap, dtype=int)
    return [items[int(i)] for i in idxs]


def compute_scene_median(frame_paths: list[Path]) -> np.ndarray:
    sample_paths = evenly_sample(frame_paths, MEDIAN_SAMPLE_CAP)
    stack = []
    for path in sample_paths:
        rgb = safe_image_to_rgb(path)
        if rgb is None:
            continue
        stack.append(rgb.astype(np.float32))
    if not stack:
        raise ValueError("No frames available to compute median image")
    median = np.median(np.stack(stack, axis=0), axis=0)
    return median.astype(np.float32)


def band_slice(h: int, band: str) -> slice:
    if band == "top":
        return slice(0, h // 3)
    if band == "mid":
        return slice(h // 3, 2 * h // 3)
    return slice(2 * h // 3, h)


def dominant_position(mask: np.ndarray) -> str | None:
    ys, xs = np.nonzero(mask)
    if len(xs) == 0:
        return None
    h, w = mask.shape
    x_mean = float(xs.mean()) / max(1, w)
    y_mean = float(ys.mean()) / max(1, h)
    if y_mean < 0.33:
        y_bucket = "upper"
    elif y_mean > 0.66:
        y_bucket = "lower"
    else:
        y_bucket = ""
    if x_mean < 0.33:
        x_bucket = "left"
    elif x_mean > 0.66:
        x_bucket = "right"
    else:
        x_bucket = "center"
    if y_bucket:
        return f"{y_bucket}-{x_bucket}"
    return x_bucket


def summarize_action(labels: dict[str, Any]) -> str:
    screen_type = labels["screen_type"]
    if screen_type == "black":
        return "Frame is effectively black."
    if screen_type == "title":
        return "Frame looks title-like or menu-like."
    if screen_type == "ocean" and not labels["sprites_visible"]:
        return "Ocean or background is visible without active sprites."
    if labels["sprites_visible"]:
        pos = labels.get("dominant_motion_position") or "unknown position"
        return f"Live sprite content is visible near {pos}."
    if screen_type == "scene":
        return "Scene background is visible, but sprite activity is weak."
    return "Visual state is ambiguous."


def compute_metrics(rgb: np.ndarray, median_rgb: np.ndarray | None = None) -> tuple[dict[str, Any], np.ndarray]:
    h, w, _ = rgb.shape
    rgb_f = rgb.astype(np.float32)
    gray_thumb = resize_gray(rgb)
    gray = np.array(Image.fromarray(rgb, mode="RGB").convert("L"), dtype=np.float32) / 255.0

    mean_rgb = rgb_f.mean(axis=(0, 1)) / 255.0
    std_rgb = rgb_f.std(axis=(0, 1)) / 255.0
    dark_ratio = float(np.mean(np.all(rgb < 20, axis=2)))

    r = rgb_f[:, :, 0]
    g = rgb_f[:, :, 1]
    b = rgb_f[:, :, 2]
    blue_ratio = float(np.mean((b > r + 20) & (b > g + 10)))
    sand_ratio = float(np.mean((r > 120) & (g > 90) & (b < 170) & (r > b + 5)))
    green_ratio = float(np.mean((g > r + 10) & (g > b + 10)))
    bright_ratio = float(np.mean(np.mean(rgb_f, axis=2) > 180))

    dx = np.abs(gray[:, 1:] - gray[:, :-1])
    dy = np.abs(gray[1:, :] - gray[:-1, :])
    edge_density = float((dx.mean() + dy.mean()) / 2.0)

    band_means = {}
    for band in ("top", "mid", "bot"):
        ys = band_slice(h, band)
        band_means[band] = (rgb_f[ys].mean(axis=(0, 1)) / 255.0).tolist()

    foreground_ratio = 0.0
    motion_bbox = None
    motion_position = None
    motion_left = motion_center = motion_right = 0.0
    person_like_ratio = 0.0
    if median_rgb is not None:
        diff = np.abs(rgb_f - median_rgb)
        diff_mean = diff.mean(axis=2)
        fg_mask = diff_mean > 22.0
        foreground_ratio = float(np.mean(fg_mask))
        ys, xs = np.nonzero(fg_mask)
        if len(xs) > 0:
            motion_bbox = {
                "x0": int(xs.min()),
                "y0": int(ys.min()),
                "x1": int(xs.max()),
                "y1": int(ys.max()),
            }
            motion_position = dominant_position(fg_mask)
            thirds = np.array_split(np.arange(w), 3)
            motion_left = float(np.mean(fg_mask[:, thirds[0]]))
            motion_center = float(np.mean(fg_mask[:, thirds[1]]))
            motion_right = float(np.mean(fg_mask[:, thirds[2]]))
            skin_mask = (
                (r > 95)
                & (g > 60)
                & (b > 45)
                & (r > g)
                & (g > b)
                & fg_mask
            )
            person_like_ratio = float(np.mean(skin_mask))

    metrics = {
        "frame_size": {"width": int(w), "height": int(h)},
        "mean_rgb": mean_rgb.tolist(),
        "std_rgb": std_rgb.tolist(),
        "dark_ratio": dark_ratio,
        "blue_ratio": blue_ratio,
        "sand_ratio": sand_ratio,
        "green_ratio": green_ratio,
        "bright_ratio": bright_ratio,
        "edge_density": edge_density,
        "foreground_ratio": foreground_ratio,
        "person_like_ratio": person_like_ratio,
        "band_means": band_means,
        "motion_bbox": motion_bbox,
        "motion_position": motion_position,
        "motion_region_ratios": {
            "left": motion_left,
            "center": motion_center,
            "right": motion_right,
        },
    }

    feature = np.concatenate(
        [
            gray_thumb.reshape(-1),
            mean_rgb.astype(np.float32),
            std_rgb.astype(np.float32),
            np.array(
                [
                    dark_ratio,
                    blue_ratio,
                    sand_ratio,
                    green_ratio,
                    bright_ratio,
                    edge_density,
                    foreground_ratio,
                    person_like_ratio,
                    motion_left,
                    motion_center,
                    motion_right,
                ],
                dtype=np.float32,
            ),
        ]
    )
    return metrics, feature.astype(np.float32)


def classify_labels(scene_id: str, metrics: dict[str, Any]) -> dict[str, Any]:
    family = scene_family(scene_id)
    dark_ratio = metrics["dark_ratio"]
    blue_ratio = metrics["blue_ratio"]
    sand_ratio = metrics["sand_ratio"]
    fg_ratio = metrics["foreground_ratio"]
    edge_density = metrics["edge_density"]
    person_like_ratio = metrics["person_like_ratio"]
    bright_ratio = metrics["bright_ratio"]

    if dark_ratio > 0.95:
        screen_type = "black"
    elif (
        blue_ratio > 0.86
        and sand_ratio < 0.045
        and edge_density < 0.0305
        and fg_ratio < 0.012
        and person_like_ratio < 0.0007
    ):
        screen_type = "ocean"
    elif dark_ratio > 0.55 and bright_ratio < 0.08 and fg_ratio < 0.03:
        screen_type = "black"
    elif fg_ratio < 0.008 and edge_density < 0.0285 and person_like_ratio < 0.0012:
        screen_type = "background"
    else:
        screen_type = "scene"

    sprites_visible = bool(
        fg_ratio > 0.016
        or person_like_ratio > 0.0018
        or (sand_ratio > 0.055 and edge_density > 0.034 and fg_ratio > 0.0075)
    )
    if fg_ratio < 0.010 and person_like_ratio < 0.001:
        sprite_density = "none"
    elif fg_ratio < 0.025:
        sprite_density = "low"
    elif fg_ratio < 0.08:
        sprite_density = "medium"
    else:
        sprite_density = "high"

    labels = {
        "screen_type": screen_type,
        "scene_family_guess": family,
        "sprites_visible": sprites_visible,
        "sprite_density": sprite_density,
        "dominant_motion_position": metrics["motion_position"],
        "human_like_visible": bool(person_like_ratio > 0.0015),
    }
    labels["action_summary"] = summarize_action(labels)
    return labels


def normalize_features(features: np.ndarray) -> np.ndarray:
    norms = np.linalg.norm(features, axis=1, keepdims=True)
    norms[norms == 0.0] = 1.0
    return features / norms


def cosine_topk(query: np.ndarray, bank: np.ndarray, k: int = 3) -> list[tuple[int, float]]:
    sims = bank @ query
    if len(sims) <= k:
        order = np.argsort(-sims)
    else:
        order = np.argpartition(-sims, k)[:k]
        order = order[np.argsort(-sims[order])]
    return [(int(i), float(sims[i])) for i in order]


def html_escape(text: str) -> str:
    return (
        str(text)
        .replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


@dataclass
class FrameRecord:
    scene_id: str
    family: str
    frame: str
    frame_number: int
    path: str
    metrics: dict[str, Any]
    labels: dict[str, Any]


def extract_scene_records(scene_dir: Path) -> tuple[list[FrameRecord], np.ndarray]:
    scene_id = scene_dir.name
    frame_paths = sample_frame_paths(scene_dir)
    if not frame_paths:
        return [], np.zeros((0, 1), dtype=np.float32)
    median_rgb = compute_scene_median(frame_paths)
    records: list[FrameRecord] = []
    features: list[np.ndarray] = []
    for frame_path in frame_paths:
        rgb = safe_image_to_rgb(frame_path)
        if rgb is None:
            continue
        metrics, feature = compute_metrics(rgb, median_rgb)
        labels = classify_labels(scene_id, metrics)
        records.append(
            FrameRecord(
                scene_id=scene_id,
                family=scene_family(scene_id),
                frame=frame_path.name,
                frame_number=frame_number(frame_path),
                path=str(frame_path),
                metrics=metrics,
                labels=labels,
            )
        )
        features.append(feature)
    return records, np.stack(features, axis=0)


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def render_scene_review(path: Path, scene_id: str, scene_summary: dict[str, Any], sample_rows: list[dict[str, Any]]) -> None:
    rows = []
    for row in sample_rows:
        labels = row["labels"]
        best = row.get("best_global_match") or {}
        expected = row.get("best_expected_match") or {}
        rows.append(
            f"""
            <tr>
              <td><img src="{html_escape(row['relative_frame_path'])}" style="width:320px; image-rendering: pixelated;"></td>
              <td>
                <div><strong>{html_escape(row['frame'])}</strong></div>
                <div>screen: {html_escape(labels['screen_type'])}</div>
                <div>sprites: {html_escape(labels['sprite_density'])}</div>
                <div>motion: {html_escape(labels.get('dominant_motion_position'))}</div>
                <div>{html_escape(labels['action_summary'])}</div>
              </td>
              <td>
                <div><strong>Expected</strong></div>
                <div>{html_escape(expected.get('scene_id'))} / {html_escape(expected.get('frame'))}</div>
                <div>score {expected.get('score', 0):.3f}</div>
                <div><strong>Global</strong></div>
                <div>{html_escape(best.get('scene_id'))} / {html_escape(best.get('frame'))}</div>
                <div>score {best.get('score', 0):.3f}</div>
              </td>
            </tr>
            """
        )
    html = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>{html_escape(scene_id)} Vision Review</title>
  <style>
    body {{ font-family: sans-serif; margin: 24px; }}
    table {{ border-collapse: collapse; width: 100%; }}
    td, th {{ border: 1px solid #ccc; vertical-align: top; padding: 8px; }}
    .meta {{ display: grid; grid-template-columns: repeat(2, minmax(200px, 1fr)); gap: 8px; margin-bottom: 20px; }}
    .card {{ border: 1px solid #ddd; padding: 8px; }}
  </style>
</head>
<body>
  <h1>{html_escape(scene_id)} Vision Review</h1>
  <div class="meta">
    <div class="card">frames: {scene_summary['frame_count']}</div>
    <div class="card">family: {html_escape(scene_summary['family'])}</div>
    <div class="card">sprites visible ratio: {scene_summary['sprites_visible_ratio']:.3f}</div>
    <div class="card">dominant screen type: {html_escape(scene_summary['dominant_screen_type'])}</div>
  </div>
  <table>
    <thead>
      <tr><th>Frame</th><th>Labels</th><th>Reference Match</th></tr>
    </thead>
    <tbody>
      {''.join(rows)}
    </tbody>
  </table>
</body>
</html>
"""
    path.write_text(html, encoding="utf-8")


def render_index(path: Path, rows: list[dict[str, Any]]) -> None:
    items = []
    for row in rows:
        items.append(
            f"<tr><td><a href=\"{html_escape(row['review_html'])}\">{html_escape(row['scene_id'])}</a></td>"
            f"<td>{html_escape(row['family'])}</td>"
            f"<td>{row['frame_count']}</td>"
            f"<td>{row['sprites_visible_ratio']:.3f}</td>"
            f"<td>{html_escape(row['dominant_screen_type'])}</td></tr>"
        )
    html = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Vision Reference Bank</title>
  <style>
    body {{ font-family: sans-serif; margin: 24px; }}
    table {{ border-collapse: collapse; width: 100%; }}
    td, th {{ border: 1px solid #ccc; padding: 8px; text-align: left; }}
  </style>
</head>
<body>
  <h1>Vision Reference Bank</h1>
  <table>
    <thead>
      <tr><th>Scene</th><th>Family</th><th>Frames</th><th>Sprites Visible Ratio</th><th>Dominant Screen Type</th></tr>
    </thead>
    <tbody>
      {''.join(items)}
    </tbody>
  </table>
</body>
</html>
"""
    path.write_text(html, encoding="utf-8")


def build_reference_bank(refdir: Path, outdir: Path) -> None:
    scene_dirs = sorted([p for p in refdir.iterdir() if p.is_dir() and sample_frame_paths(p)])
    outdir.mkdir(parents=True, exist_ok=True)
    all_records: list[FrameRecord] = []
    per_scene_features: dict[str, np.ndarray] = {}

    for index, scene_dir in enumerate(scene_dirs, start=1):
        print(f"[{index}/{len(scene_dirs)}] extracting {scene_dir.name}", file=sys.stderr, flush=True)
        records, features = extract_scene_records(scene_dir)
        if not records:
            continue
        all_records.extend(records)
        per_scene_features[scene_dir.name] = features

    if not all_records:
        raise SystemExit("No reference frames found")

    feature_matrix = np.concatenate([per_scene_features[s.name] for s in scene_dirs if s.name in per_scene_features], axis=0)
    norm_features = normalize_features(feature_matrix.astype(np.float32))

    metadata = []
    scene_offsets: dict[str, tuple[int, int]] = {}
    cursor = 0
    for scene_dir in scene_dirs:
        feats = per_scene_features.get(scene_dir.name)
        if feats is None:
            continue
        count = feats.shape[0]
        scene_offsets[scene_dir.name] = (cursor, cursor + count)
        cursor += count

    for record in all_records:
        metadata.append(
            {
                "scene_id": record.scene_id,
                "family": record.family,
                "frame": record.frame,
                "frame_number": record.frame_number,
                "path": record.path,
                "metrics": record.metrics,
                "labels": record.labels,
            }
        )

    bank_dir = outdir
    bank_dir.mkdir(parents=True, exist_ok=True)
    np.save(bank_dir / "features.npy", norm_features)
    write_json(bank_dir / "metadata.json", {"frames": metadata, "scene_offsets": scene_offsets})

    scene_rows = []
    by_scene: dict[str, list[int]] = defaultdict(list)
    for i, meta in enumerate(metadata):
        by_scene[meta["scene_id"]].append(i)

    scene_prototypes = {}
    for scene_id, idxs in by_scene.items():
        scene_prototypes[scene_id] = normalize_features(norm_features[idxs].mean(axis=0, keepdims=True))[0]

    prototype_stack = np.stack([scene_prototypes[sid] for sid in sorted(scene_prototypes)], axis=0)
    prototype_names = sorted(scene_prototypes)

    for index, scene_id in enumerate(sorted(by_scene), start=1):
        print(f"[{index}/{len(by_scene)}] rendering {scene_id}", file=sys.stderr, flush=True)
        idxs = by_scene[scene_id]
        scene_meta = [metadata[i] for i in idxs]
        label_counter = Counter(m["labels"]["screen_type"] for m in scene_meta)
        sprites_visible_ratio = float(np.mean([1.0 if m["labels"]["sprites_visible"] else 0.0 for m in scene_meta]))
        scene_summary = {
            "scene_id": scene_id,
            "family": scene_family(scene_id),
            "frame_count": len(scene_meta),
            "sprites_visible_ratio": sprites_visible_ratio,
            "dominant_screen_type": label_counter.most_common(1)[0][0],
        }

        sample_local = evenly_sample([Path(m["path"]) for m in scene_meta], min(12, len(scene_meta)))
        sample_rows = []
        scene_feature_idx = {metadata[i]["frame"]: i for i in idxs}
        expected_proto = scene_prototypes[scene_id]
        for sample_path in sample_local:
            meta = next(m for m in scene_meta if m["frame"] == sample_path.name)
            frame_idx = scene_feature_idx[sample_path.name]
            query = norm_features[frame_idx]
            proto_scores = prototype_stack @ query
            best_global_i = int(np.argmax(proto_scores))
            best_global_scene = prototype_names[best_global_i]
            sample_rows.append(
                {
                    "frame": sample_path.name,
                    "relative_frame_path": os.path.relpath(sample_path, (bank_dir / "scenes" / scene_id).resolve()),
                    "labels": meta["labels"],
                    "best_expected_match": {
                        "scene_id": scene_id,
                        "frame": meta["frame"],
                        "score": float(query @ expected_proto),
                    },
                    "best_global_match": {
                        "scene_id": best_global_scene,
                        "frame": meta["frame"] if best_global_scene == scene_id else "",
                        "score": float(proto_scores[best_global_i]),
                    },
                }
            )

        scene_dir_out = bank_dir / "scenes" / scene_id
        write_json(scene_dir_out / "analysis.json", {"scene": scene_summary, "frames": scene_meta})
        render_scene_review(scene_dir_out / "review.html", scene_id, scene_summary, sample_rows)
        scene_rows.append(
            {
                "scene_id": scene_id,
                "family": scene_summary["family"],
                "frame_count": scene_summary["frame_count"],
                "sprites_visible_ratio": scene_summary["sprites_visible_ratio"],
                "dominant_screen_type": scene_summary["dominant_screen_type"],
                "review_html": f"scenes/{scene_id}/review.html",
            }
        )

    write_json(bank_dir / "index.json", {"scenes": scene_rows, "frame_count": len(metadata)})
    render_index(bank_dir / "index.html", scene_rows)


def analyze_run(scene_dir: Path, bank_dir: Path, outdir: Path, expected_scene: str | None) -> None:
    bank_features = np.load(bank_dir / "features.npy")
    bank_meta = json.loads((bank_dir / "metadata.json").read_text())
    frames_meta = bank_meta["frames"]
    scene_offsets = bank_meta["scene_offsets"]

    records, features = extract_scene_records(scene_dir)
    norm_features = normalize_features(features)
    expected_scene = expected_scene or scene_dir.name
    expected_range = scene_offsets.get(expected_scene)

    output_frames = []
    for record, query in zip(records, norm_features):
        global_hits = cosine_topk(query, bank_features, k=3)
        best_global_idx, best_global_score = global_hits[0]
        best_global = frames_meta[best_global_idx]

        best_expected = None
        if expected_range:
            lo, hi = expected_range
            expected_hits = cosine_topk(query, bank_features[lo:hi], k=3)
            expected_idx, expected_score = expected_hits[0]
            best_expected = frames_meta[lo + expected_idx] | {"score": expected_score}

        output_frames.append(
            {
                "frame": record.frame,
                "frame_number": record.frame_number,
                "path": record.path,
                "metrics": record.metrics,
                "labels": record.labels,
                "best_global_match": {
                    "scene_id": best_global["scene_id"],
                    "frame": best_global["frame"],
                    "score": best_global_score,
                },
                "best_expected_match": None
                if best_expected is None
                else {
                    "scene_id": best_expected["scene_id"],
                    "frame": best_expected["frame"],
                    "score": best_expected["score"],
                },
            }
        )

    dominant_failure_mode = "unknown"
    ocean_ratio = float(np.mean([1.0 if row["labels"]["screen_type"] == "ocean" else 0.0 for row in output_frames])) if output_frames else 0.0
    sprite_ratio = float(np.mean([1.0 if row["labels"]["sprites_visible"] else 0.0 for row in output_frames])) if output_frames else 0.0
    global_scene_counter = Counter(row["best_global_match"]["scene_id"] for row in output_frames)
    if ocean_ratio > 0.5 and sprite_ratio < 0.2:
        dominant_failure_mode = "background_only_missing_sprites"
    elif global_scene_counter and global_scene_counter.most_common(1)[0][0] != expected_scene:
        dominant_failure_mode = "wrong_family_or_wrong_scene"

    summary = {
        "scene": expected_scene,
        "frames_analyzed": len(output_frames),
        "sprite_visible_ratio": sprite_ratio,
        "ocean_ratio": ocean_ratio,
        "dominant_global_match_scene": global_scene_counter.most_common(1)[0][0] if global_scene_counter else None,
        "dominant_failure_mode": dominant_failure_mode,
    }
    write_json(outdir / "vision-analysis.json", {"summary": summary, "frames": output_frames})

    sample_frames = evenly_sample(sample_frame_paths(scene_dir), min(12, len(output_frames)))
    sample_rows = []
    frame_map = {row["frame"]: row for row in output_frames}
    for frame_path in sample_frames:
        row = frame_map.get(frame_path.name)
        if row is None:
            continue
        sample_rows.append(
            {
                "frame": row["frame"],
                "relative_frame_path": os.path.relpath(frame_path, outdir.resolve()),
                "labels": row["labels"],
                "best_expected_match": row["best_expected_match"],
                "best_global_match": row["best_global_match"],
            }
        )
    render_scene_review(
        outdir / "review.html",
        expected_scene,
        {
            "scene_id": expected_scene,
            "family": scene_family(expected_scene),
            "frame_count": len(output_frames),
            "sprites_visible_ratio": sprite_ratio,
            "dominant_screen_type": Counter(row["labels"]["screen_type"] for row in output_frames).most_common(1)[0][0]
            if output_frames
            else "unknown",
        },
        sample_rows,
    )


def analyze_reference_set(refdir: Path, bank_dir: Path, outdir: Path) -> None:
    scene_dirs = sorted([p for p in refdir.iterdir() if p.is_dir() and sample_frame_paths(p)])
    outdir.mkdir(parents=True, exist_ok=True)
    rows = []
    for index, scene_dir in enumerate(scene_dirs, start=1):
        print(f"[{index}/{len(scene_dirs)}] analyzing {scene_dir.name}", file=sys.stderr, flush=True)
        scene_out = outdir / "scenes" / scene_dir.name
        analyze_run(scene_dir, bank_dir, scene_out, scene_dir.name)
        analysis_path = scene_out / "vision-analysis.json"
        data = json.loads(analysis_path.read_text())
        summary = data["summary"]
        rows.append(
            {
                "scene_id": scene_dir.name,
                "family": scene_family(scene_dir.name),
                "frame_count": summary["frames_analyzed"],
                "sprites_visible_ratio": summary["sprite_visible_ratio"],
                "dominant_screen_type": Counter(
                    frame["labels"]["screen_type"] for frame in data["frames"]
                ).most_common(1)[0][0]
                if data["frames"]
                else "unknown",
                "review_html": f"scenes/{scene_dir.name}/review.html",
            }
        )
    write_json(outdir / "index.json", {"scenes": rows, "scene_count": len(rows)})
    render_index(outdir / "index.html", rows)
    generate_quality_report(outdir)
    generate_confusion_report(outdir)
    generate_family_report(outdir)


def generate_quality_report(outdir: Path) -> None:
    scene_root = outdir / "scenes"
    rows = []
    for scene_dir in sorted(scene_root.glob("*")):
        p = scene_dir / "vision-analysis.json"
        if not p.exists():
            continue
        data = json.loads(p.read_text())
        frames = data["frames"]
        expected = data["summary"]["scene"]
        expected_top1 = 0
        global_top1 = 0
        for frame in frames:
            expected_match = frame.get("best_expected_match") or {}
            global_match = frame.get("best_global_match") or {}
            if expected_match.get("scene_id") == expected:
                expected_top1 += 1
            if global_match.get("scene_id") == expected:
                global_top1 += 1
        frame_count = max(1, len(frames))
        rows.append(
            {
                "scene_id": expected,
                "expected_top1_ratio": expected_top1 / frame_count,
                "global_top1_ratio": global_top1 / frame_count,
                "sprite_visible_ratio": data["summary"]["sprite_visible_ratio"],
                "ocean_ratio": data["summary"]["ocean_ratio"],
                "dominant_global_match_scene": data["summary"]["dominant_global_match_scene"],
                "dominant_failure_mode": data["summary"]["dominant_failure_mode"],
                "review_html": f"scenes/{expected}/review.html",
            }
        )
    write_json(outdir / "quality-report.json", {"scenes": rows, "scene_count": len(rows)})

    tr = []
    for row in rows:
        tr.append(
            f"<tr><td><a href=\"{html_escape(row['review_html'])}\">{html_escape(row['scene_id'])}</a></td>"
            f"<td>{row['expected_top1_ratio']:.3f}</td>"
            f"<td>{row['global_top1_ratio']:.3f}</td>"
            f"<td>{row['sprite_visible_ratio']:.3f}</td>"
            f"<td>{row['ocean_ratio']:.3f}</td>"
            f"<td>{html_escape(row['dominant_global_match_scene'])}</td>"
            f"<td>{html_escape(row['dominant_failure_mode'])}</td></tr>"
        )
    html = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Vision Quality Report</title>
  <style>
    body {{ font-family: sans-serif; margin: 24px; }}
    table {{ border-collapse: collapse; width: 100%; }}
    td, th {{ border: 1px solid #ccc; padding: 8px; text-align: left; }}
  </style>
</head>
<body>
  <h1>Vision Quality Report</h1>
  <table>
    <thead>
      <tr><th>Scene</th><th>Expected Top1</th><th>Global Top1</th><th>Sprites</th><th>Ocean Ratio</th><th>Dominant Global</th><th>Failure Mode</th></tr>
    </thead>
    <tbody>
      {''.join(tr)}
    </tbody>
  </table>
</body>
</html>
"""
    (outdir / "quality-report.html").write_text(html, encoding="utf-8")


def generate_confusion_report(outdir: Path) -> None:
    scene_root = outdir / "scenes"
    rows = []
    for scene_dir in sorted(scene_root.glob("*")):
        p = scene_dir / "vision-analysis.json"
        if not p.exists():
            continue
        data = json.loads(p.read_text())
        expected = data["summary"]["scene"]
        counter = Counter()
        for frame in data["frames"]:
            match = frame.get("best_global_match") or {}
            sid = match.get("scene_id")
            if sid:
                counter[sid] += 1
        total = sum(counter.values()) or 1
        alternates = []
        for sid, count in counter.most_common(5):
            if sid == expected:
                continue
            alternates.append(
                {
                    "scene_id": sid,
                    "ratio": count / total,
                    "count": count,
                }
            )
        rows.append(
            {
                "scene_id": expected,
                "dominant_global_match_scene": data["summary"]["dominant_global_match_scene"],
                "global_top1_ratio": max((counter.get(expected, 0) / total), 0.0),
                "alternates": alternates,
                "review_html": f"scenes/{expected}/review.html",
            }
        )
    write_json(outdir / "confusion-report.json", {"scenes": rows, "scene_count": len(rows)})

    tr = []
    for row in sorted(rows, key=lambda r: (r["global_top1_ratio"], r["scene_id"])):
        alt_text = ", ".join(
            f"{alt['scene_id']} ({alt['ratio']:.3f})" for alt in row["alternates"][:3]
        ) or "-"
        tr.append(
            f"<tr><td><a href=\"{html_escape(row['review_html'])}\">{html_escape(row['scene_id'])}</a></td>"
            f"<td>{html_escape(row['dominant_global_match_scene'])}</td>"
            f"<td>{row['global_top1_ratio']:.3f}</td>"
            f"<td>{html_escape(alt_text)}</td></tr>"
        )
    html = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Vision Confusion Report</title>
  <style>
    body {{ font-family: sans-serif; margin: 24px; }}
    table {{ border-collapse: collapse; width: 100%; }}
    td, th {{ border: 1px solid #ccc; padding: 8px; text-align: left; }}
  </style>
</head>
<body>
  <h1>Vision Confusion Report</h1>
  <table>
    <thead>
      <tr><th>Scene</th><th>Dominant Global Match</th><th>Global Top1</th><th>Top Alternate Scenes</th></tr>
    </thead>
    <tbody>
      {''.join(tr)}
    </tbody>
  </table>
</body>
</html>
"""
    (outdir / "confusion-report.html").write_text(html, encoding="utf-8")


def generate_family_report(outdir: Path) -> None:
    quality = json.loads((outdir / "quality-report.json").read_text())
    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in quality["scenes"]:
        grouped[scene_family(row["scene_id"])].append(row)

    family_rows = []
    weak_rows = []
    for family, rows in sorted(grouped.items()):
        n = max(1, len(rows))
        family_rows.append(
            {
                "family": family,
                "scene_count": len(rows),
                "avg_expected_top1": sum(r["expected_top1_ratio"] for r in rows) / n,
                "avg_global_top1": sum(r["global_top1_ratio"] for r in rows) / n,
                "avg_sprite_visible": sum(r["sprite_visible_ratio"] for r in rows) / n,
                "avg_ocean_ratio": sum(r["ocean_ratio"] for r in rows) / n,
            }
        )
        weak_rows.extend(rows)

    weak_rows.sort(key=lambda r: (r["global_top1_ratio"], r["scene_id"]))
    weak_rows = weak_rows[:20]

    write_json(
        outdir / "family-report.json",
        {"families": family_rows, "weakest_scenes": weak_rows, "family_count": len(family_rows)},
    )

    fam_tr = []
    for row in family_rows:
        fam_tr.append(
            f"<tr><td>{html_escape(row['family'])}</td>"
            f"<td>{row['scene_count']}</td>"
            f"<td>{row['avg_expected_top1']:.3f}</td>"
            f"<td>{row['avg_global_top1']:.3f}</td>"
            f"<td>{row['avg_sprite_visible']:.3f}</td>"
            f"<td>{row['avg_ocean_ratio']:.3f}</td></tr>"
        )

    weak_tr = []
    for row in weak_rows:
        weak_tr.append(
            f"<tr><td><a href=\"{html_escape(row['review_html'])}\">{html_escape(row['scene_id'])}</a></td>"
            f"<td>{row['global_top1_ratio']:.3f}</td>"
            f"<td>{html_escape(row['dominant_global_match_scene'])}</td>"
            f"<td>{html_escape(row['dominant_failure_mode'])}</td></tr>"
        )

    html = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Vision Family Report</title>
  <style>
    body {{ font-family: sans-serif; margin: 24px; }}
    table {{ border-collapse: collapse; width: 100%; margin-bottom: 24px; }}
    td, th {{ border: 1px solid #ccc; padding: 8px; text-align: left; }}
  </style>
</head>
<body>
  <h1>Vision Family Report</h1>
  <h2>Family Summary</h2>
  <table>
    <thead>
      <tr><th>Family</th><th>Scenes</th><th>Avg Expected Top1</th><th>Avg Global Top1</th><th>Avg Sprites</th><th>Avg Ocean</th></tr>
    </thead>
    <tbody>{''.join(fam_tr)}</tbody>
  </table>
  <h2>Weakest Scenes</h2>
  <table>
    <thead>
      <tr><th>Scene</th><th>Global Top1</th><th>Dominant Global</th><th>Failure Mode</th></tr>
    </thead>
    <tbody>{''.join(weak_tr)}</tbody>
  </table>
</body>
</html>
"""
    (outdir / "family-report.html").write_text(html, encoding="utf-8")


def parse_args() -> argparse.Namespace:
    project_root = find_project_root()
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)

    build = sub.add_parser("build-reference-bank", help="Build reference-bank artifacts")
    build.add_argument("--refdir", type=Path, default=project_root / "regtest-references")
    build.add_argument("--outdir", type=Path, required=True)

    analyze = sub.add_parser("analyze-run", help="Analyze a run against an existing bank")
    analyze.add_argument("--scene-dir", type=Path, required=True)
    analyze.add_argument("--bank-dir", type=Path, required=True)
    analyze.add_argument("--outdir", type=Path, required=True)
    analyze.add_argument("--expected-scene", type=str)

    analyze_set = sub.add_parser("analyze-reference-set", help="Analyze all reference scenes against an existing bank")
    analyze_set.add_argument("--refdir", type=Path, default=project_root / "regtest-references")
    analyze_set.add_argument("--bank-dir", type=Path, required=True)
    analyze_set.add_argument("--outdir", type=Path, required=True)

    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.cmd == "build-reference-bank":
        build_reference_bank(args.refdir, args.outdir)
    elif args.cmd == "analyze-run":
        analyze_run(args.scene_dir, args.bank_dir, args.outdir, args.expected_scene)
    elif args.cmd == "analyze-reference-set":
        analyze_reference_set(args.refdir, args.bank_dir, args.outdir)


if __name__ == "__main__":
    main()
