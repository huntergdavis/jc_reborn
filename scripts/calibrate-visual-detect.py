#!/usr/bin/env python3
"""
Calibrate visual detection parameters from reference frames.

Reads captured reference frames from regtest-references/ and extracts actual
color values for key visual elements (sky, water, sand, telemetry panel).
Outputs a calibration JSON that the visual detection system can use.

Usage:
    ./scripts/calibrate-visual-detect.py
    ./scripts/calibrate-visual-detect.py --refdir regtest-references/
    ./scripts/calibrate-visual-detect.py --verbose
    ./scripts/calibrate-visual-detect.py --scene STAND-1

Output: regtest-references/calibration.json
"""

import argparse
import json
import os
import sys
from collections import Counter, defaultdict
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print(
        "ERROR: Pillow is required. Install with: pip install Pillow",
        file=sys.stderr,
    )
    sys.exit(1)


def find_project_root() -> Path:
    here = Path(__file__).resolve().parent
    root = here.parent
    if (root / "config" / "ps1" / "regtest-scenes.txt").is_file():
        return root
    return Path.cwd()


# PS1 framebuffer is 320x240 (or 256x240 depending on mode).
# DuckStation regtest captures at native resolution.
FB_WIDTH = 320
FB_HEIGHT = 240


def sample_region(img: Image.Image, x0: int, y0: int, x1: int, y1: int) -> list[tuple]:
    """Sample all pixels in a rectangular region. Returns list of (r,g,b) tuples."""
    pixels = []
    w, h = img.size
    # Clamp to image bounds
    x0 = max(0, min(x0, w - 1))
    y0 = max(0, min(y0, h - 1))
    x1 = max(0, min(x1, w))
    y1 = max(0, min(y1, h))

    for y in range(y0, y1):
        for x in range(x0, x1):
            px = img.getpixel((x, y))
            if len(px) >= 3:
                pixels.append((px[0], px[1], px[2]))
    return pixels


def dominant_colors(pixels: list[tuple], top_n: int = 5) -> list[dict]:
    """Find the most common colors in a pixel sample."""
    if not pixels:
        return []
    counter = Counter(pixels)
    return [
        {"r": c[0], "g": c[1], "b": c[2], "count": n}
        for c, n in counter.most_common(top_n)
    ]


def average_color(pixels: list[tuple]) -> dict | None:
    """Compute average RGB from a pixel sample."""
    if not pixels:
        return None
    n = len(pixels)
    r = sum(p[0] for p in pixels) // n
    g = sum(p[1] for p in pixels) // n
    b = sum(p[2] for p in pixels) // n
    return {"r": r, "g": g, "b": b}


def is_mostly_black(pixels: list[tuple], threshold: int = 20) -> bool:
    """Check if most pixels are near-black."""
    if not pixels:
        return True
    dark = sum(1 for r, g, b in pixels if r < threshold and g < threshold and b < threshold)
    return dark > len(pixels) * 0.8


def analyze_frame(img: Image.Image) -> dict:
    """Analyze a single frame for calibration data."""
    w, h = img.size

    # Define sampling regions (relative to frame dimensions)
    # Sky: top 20% of frame, center horizontal strip (avoid telemetry on left)
    sky_x0 = w // 4
    sky_x1 = w * 3 // 4
    sky_y0 = 0
    sky_y1 = h // 5

    # Water: bottom 20%, center strip
    water_x0 = w // 4
    water_x1 = w * 3 // 4
    water_y0 = h * 4 // 5
    water_y1 = h

    # Island/sand: middle band where sand typically appears
    # (roughly y=180..380 in native 240p, scaled proportionally)
    island_y0 = int(h * 0.45)
    island_y1 = int(h * 0.75)
    island_x0 = w // 4
    island_x1 = w * 3 // 4

    # Telemetry panel: leftmost ~70px, full height
    telem_x0 = 0
    telem_x1 = min(70, w // 4)
    telem_y0 = 0
    telem_y1 = h

    sky_pixels = sample_region(img, sky_x0, sky_y0, sky_x1, sky_y1)
    water_pixels = sample_region(img, water_x0, water_y0, water_x1, water_y1)
    island_pixels = sample_region(img, island_x0, island_y0, island_x1, island_y1)
    telem_pixels = sample_region(img, telem_x0, telem_y0, telem_x1, telem_y1)

    return {
        "sky": {
            "pixels_sampled": len(sky_pixels),
            "average": average_color(sky_pixels),
            "dominant": dominant_colors(sky_pixels, top_n=3),
            "region": {"x0": sky_x0, "y0": sky_y0, "x1": sky_x1, "y1": sky_y1},
        },
        "water": {
            "pixels_sampled": len(water_pixels),
            "average": average_color(water_pixels),
            "dominant": dominant_colors(water_pixels, top_n=5),
            "region": {"x0": water_x0, "y0": water_y0, "x1": water_x1, "y1": water_y1},
        },
        "island": {
            "pixels_sampled": len(island_pixels),
            "average": average_color(island_pixels),
            "dominant": dominant_colors(island_pixels, top_n=8),
            "region": {"x0": island_x0, "y0": island_y0, "x1": island_x1, "y1": island_y1},
        },
        "telemetry_panel": {
            "pixels_sampled": len(telem_pixels),
            "average": average_color(telem_pixels),
            "dominant": dominant_colors(telem_pixels, top_n=3),
            "region": {"x0": telem_x0, "y0": telem_y0, "x1": telem_x1, "y1": telem_y1},
            "is_mostly_black": is_mostly_black(telem_pixels),
        },
        "frame_size": {"width": w, "height": h},
        "is_black_frame": is_mostly_black(
            sample_region(img, w // 4, h // 4, w * 3 // 4, h * 3 // 4)
        ),
    }


def classify_frame(analysis: dict) -> str:
    """Classify a frame as 'scene', 'title', 'black', or 'unknown'."""
    if analysis["is_black_frame"]:
        return "black"

    sky = analysis["sky"]
    if sky["average"]:
        r, g, b = sky["average"]["r"], sky["average"]["g"], sky["average"]["b"]
        # The JC sky is a distinctive cyan: low R, high G, high B
        if r < 50 and g > 200 and b > 200:
            return "scene"

    return "unknown"


def calibrate_from_scenes(ref_dir: Path, scene_filter: str | None, verbose: bool) -> dict:
    """Process all reference frames and compute calibration data."""
    sky_colors = []
    water_colors = []
    sand_colors = []
    telem_colors = []
    frame_analyses = []
    scene_summaries = {}
    special_summaries = {}

    # Collect directories to process
    dirs_to_process = []

    if ref_dir.is_dir():
        for subdir in sorted(ref_dir.iterdir()):
            if not subdir.is_dir() or subdir.name.startswith("."):
                continue
            if scene_filter and subdir.name != scene_filter:
                continue
            dirs_to_process.append(subdir)

    if not dirs_to_process:
        print("WARNING: No scene directories found to process.", file=sys.stderr)
        return {}

    total_frames = 0

    for scene_dir in dirs_to_process:
        frame_files = sorted(scene_dir.glob("**/frame_*.png"))
        if not frame_files:
            continue

        is_special = scene_dir.name in {"title", "transitions", "ocean", "black"}
        dir_label = scene_dir.name

        if verbose:
            print(f"  Processing {dir_label}: {len(frame_files)} frames", file=sys.stderr)

        scene_sky = []
        scene_water = []
        scene_island = []
        scene_classifications = Counter()

        # Sample a subset of frames (every 3rd frame for efficiency)
        sample_step = max(1, len(frame_files) // 20)
        sampled_files = frame_files[::sample_step]
        # Always include first and last
        if frame_files[0] not in sampled_files:
            sampled_files.insert(0, frame_files[0])
        if frame_files[-1] not in sampled_files:
            sampled_files.append(frame_files[-1])

        for frame_path in sampled_files:
            try:
                img = Image.open(frame_path).convert("RGB")
            except (OSError, IOError) as e:
                if verbose:
                    print(f"    WARNING: Cannot open {frame_path.name}: {e}", file=sys.stderr)
                continue

            analysis = analyze_frame(img)
            classification = classify_frame(analysis)
            scene_classifications[classification] += 1
            total_frames += 1

            # Collect non-black frame data for calibration
            if not analysis["is_black_frame"]:
                if analysis["sky"]["average"]:
                    sky_colors.append(analysis["sky"]["average"])
                    scene_sky.append(analysis["sky"]["average"])
                if analysis["water"]["average"]:
                    water_colors.append(analysis["water"]["average"])
                    scene_water.append(analysis["water"]["average"])
                if analysis["island"]["dominant"]:
                    for color in analysis["island"]["dominant"][:3]:
                        c = {"r": color["r"], "g": color["g"], "b": color["b"]}
                        sand_colors.append(c)
                        scene_island.append(c)
                if analysis["telemetry_panel"]["average"]:
                    telem_colors.append(analysis["telemetry_panel"]["average"])

            frame_analyses.append({
                "dir": dir_label,
                "frame": frame_path.name,
                "classification": classification,
            })

        # Scene summary
        summary = {
            "frames_sampled": len(sampled_files),
            "frames_total": len(frame_files),
            "classifications": dict(scene_classifications),
            "sky_average": average_color(
                [(c["r"], c["g"], c["b"]) for c in scene_sky]
            ) if scene_sky else None,
            "water_average": average_color(
                [(c["r"], c["g"], c["b"]) for c in scene_water]
            ) if scene_water else None,
        }

        if is_special:
            special_summaries[dir_label] = summary
        else:
            scene_summaries[dir_label] = summary

    # Build calibration output
    calibration = {
        "version": 1,
        "generated": __import__("datetime").datetime.now(__import__("datetime").timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "frames_analyzed": total_frames,
        "frame_resolution": {"width": FB_WIDTH, "height": FB_HEIGHT},
    }

    # Sky color calibration
    if sky_colors:
        sky_rgb = [(c["r"], c["g"], c["b"]) for c in sky_colors]
        calibration["sky_color"] = average_color(sky_rgb)
        calibration["sky_colors_dominant"] = dominant_colors(sky_rgb, top_n=5)
    else:
        calibration["sky_color"] = None
        calibration["sky_colors_dominant"] = []

    # Water color calibration
    if water_colors:
        water_rgb = [(c["r"], c["g"], c["b"]) for c in water_colors]
        calibration["water_color"] = average_color(water_rgb)
        calibration["water_colors"] = dominant_colors(water_rgb, top_n=8)
    else:
        calibration["water_color"] = None
        calibration["water_colors"] = []

    # Sand/island color calibration
    if sand_colors:
        sand_rgb = [(c["r"], c["g"], c["b"]) for c in sand_colors]
        calibration["sand_colors"] = dominant_colors(sand_rgb, top_n=10)
    else:
        calibration["sand_colors"] = []

    # Island typical Y range (proportional to 240p)
    calibration["island_typical_y_range"] = [
        int(FB_HEIGHT * 0.45),
        int(FB_HEIGHT * 0.75),
    ]

    # Telemetry panel rect and background color
    calibration["telemetry_panel_rect"] = [0, 0, 70, FB_HEIGHT]
    if telem_colors:
        telem_rgb = [(c["r"], c["g"], c["b"]) for c in telem_colors]
        calibration["telemetry_panel_bg"] = average_color(telem_rgb)
    else:
        calibration["telemetry_panel_bg"] = {"r": 0, "g": 0, "b": 0}

    # Sampling regions used
    calibration["sampling_regions"] = {
        "sky": {
            "description": "Top 20% of frame, center horizontal strip (avoids telemetry)",
            "x_range": "25%-75% of width",
            "y_range": "0%-20% of height",
        },
        "water": {
            "description": "Bottom 20% of frame, center horizontal strip",
            "x_range": "25%-75% of width",
            "y_range": "80%-100% of height",
        },
        "island": {
            "description": "Middle band where sand/island typically appears",
            "x_range": "25%-75% of width",
            "y_range": "45%-75% of height",
        },
        "telemetry_panel": {
            "description": "Leftmost strip containing diagnostic bars",
            "x_range": "0-70px",
            "y_range": "full height",
        },
    }

    # Per-scene and special screen summaries
    calibration["scene_summaries"] = scene_summaries
    calibration["special_summaries"] = special_summaries

    return calibration


def main():
    parser = argparse.ArgumentParser(
        description="Calibrate visual detection from reference frames."
    )
    parser.add_argument(
        "--refdir",
        default=None,
        help="Path to regtest-references/ directory",
    )
    parser.add_argument(
        "--scene",
        default=None,
        help="Process only a specific scene directory (e.g., STAND-1)",
    )
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Verbose output",
    )
    parser.add_argument(
        "--stdout",
        action="store_true",
        help="Print calibration to stdout instead of writing file",
    )
    args = parser.parse_args()

    project_root = find_project_root()

    if args.refdir:
        ref_dir = Path(args.refdir).resolve()
    else:
        ref_dir = project_root / "regtest-references"

    if not ref_dir.is_dir():
        print(f"ERROR: Reference directory not found: {ref_dir}", file=sys.stderr)
        print("Run ./scripts/capture-reference-frames.sh first.", file=sys.stderr)
        sys.exit(1)

    # Check for any frames
    has_frames = any(
        subdir.is_dir() and list(subdir.glob("**/frame_*.png"))
        for subdir in ref_dir.iterdir()
        if subdir.is_dir() and not subdir.name.startswith(".")
    )
    if not has_frames:
        print("ERROR: No reference frames found in", ref_dir, file=sys.stderr)
        print("Run ./scripts/capture-reference-frames.sh first.", file=sys.stderr)
        sys.exit(1)

    if args.verbose:
        print(f"Scanning reference frames in {ref_dir}", file=sys.stderr)

    calibration = calibrate_from_scenes(ref_dir, args.scene, args.verbose)

    if not calibration:
        print("ERROR: No calibration data produced.", file=sys.stderr)
        sys.exit(1)

    output = json.dumps(calibration, indent=2, ensure_ascii=False)

    if args.stdout:
        print(output)
    else:
        cal_file = ref_dir / "calibration.json"
        cal_file.write_text(output + "\n", encoding="utf-8")
        print(f"Calibration written to {cal_file}", file=sys.stderr)
        print(f"  Frames analyzed: {calibration['frames_analyzed']}", file=sys.stderr)
        if calibration.get("sky_color"):
            sc = calibration["sky_color"]
            print(f"  Sky color:       rgb({sc['r']}, {sc['g']}, {sc['b']})", file=sys.stderr)
        if calibration.get("water_color"):
            wc = calibration["water_color"]
            print(f"  Water color:     rgb({wc['r']}, {wc['g']}, {wc['b']})", file=sys.stderr)
        sand_n = len(calibration.get("sand_colors", []))
        print(f"  Sand colors:     {sand_n} dominant colors extracted", file=sys.stderr)
        print(f"  Telemetry rect:  {calibration.get('telemetry_panel_rect')}", file=sys.stderr)


if __name__ == "__main__":
    main()
