#!/usr/bin/env python3
"""
Visual scene and sprite detection for Johnny Castaway PS1 port.

Analyzes screenshots to identify:
- Screen type (title, ocean, island scene, black/fade)
- Scene family (STAND, JOHNNY, ACTIVITY, FISHING, etc.)
- Sprite presence (Johnny, palm tree, raft, clouds, etc.)
- Confidence scores for all detections

The PS1 port uses a fixed 16-color palette.  This module does pure
algorithmic detection -- no ML, no training data.

Usage:
    python3 scripts/visual_detect.py screenshot.png
    python3 scripts/visual_detect.py --json screenshot.png
    python3 scripts/visual_detect.py --batch dir_of_pngs/
    python3 scripts/visual_detect.py --expect "STAND 2" screenshot.png
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

try:
    from PIL import Image
except ImportError:
    print("error: Pillow is required (pip install pillow)", file=sys.stderr)
    raise

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

# Standard framebuffer sizes emitted by DuckStation regtest captures.
# Early BIOS/boot frames are 256x224; game frames are 640x448.
NATIVE_W = 640
NATIVE_H = 448  # interlaced PAL/NTSC capture is 448 not 480

# The PS1 port uses exactly 16 colors, derived from the Sierra palette
# stored as RGB555 in VRAM.  The 8-bit RGB equivalents (measured from
# real DuckStation captures) are listed here.
#
#   Name           RGB              Notes
#   ----           ---              -----
#   black          (0,   0,   0)    transparent / void
#   near_black     (9,   0,   0)    wave detail / shadow in water
#   dark_blue      (0,   0,   173)  deep water
#   blue           (0,   0,   255)  primary water
#   dark_cyan      (0,   173, 173)  horizon / waveline
#   cyan           (0,   255, 255)  sky
#   dark_green     (0,   173, 0)    palm leaves shadow
#   green          (0,   255, 0)    palm leaves
#   dark_red       (173, 0,   0)    palm trunk / raft
#   red            (255, 0,   0)    Johnny shirt / sprite accent
#   dark_yellow    (173, 173, 0)    sand shadow / dark sand
#   yellow         (255, 255, 0)    sand
#   magenta        (255, 0,   255)  rare / special (title text?)
#   dark_magenta   (173, 0,   173)  title screen element
#   gray           (128, 128, 128)  sand detail / trunk detail
#   light_gray     (210, 210, 210)  cloud / sand highlight
#   white          (255, 255, 255)  cloud / highlight
#
# For matching we use tolerance bands because DuckStation may shift values
# slightly depending on capture backend.

# Canonical palette entries as (R, G, B).
PALETTE: Dict[str, Tuple[int, int, int]] = {
    "black":         (0,   0,   0),
    "near_black":    (9,   0,   0),
    "dark_blue":     (0,   0,   173),
    "blue":          (0,   0,   255),
    "dark_cyan":     (0,   173, 173),
    "cyan":          (0,   255, 255),
    "dark_green":    (0,   173, 0),
    "green":         (0,   255, 0),
    "dark_red":      (173, 0,   0),
    "red":           (255, 0,   0),
    "dark_yellow":   (173, 173, 0),
    "yellow":        (255, 255, 0),
    "magenta":       (255, 0,   255),
    "dark_magenta":  (173, 0,   173),
    "gray":          (128, 128, 128),
    "light_gray":    (210, 210, 210),
    "white":         (255, 255, 255),
}

# Build a fast lookup: map exact RGB -> palette name.
_PALETTE_EXACT: Dict[Tuple[int, int, int], str] = {v: k for k, v in PALETTE.items()}

# Tolerance for fuzzy palette matching (per-channel).
_PALETTE_TOL = 20

# Telemetry debug panel area (VRAM pixel coordinates).
# Bars are rendered in the top-left corner.  We exclude this region from
# scene analysis to avoid misclassifying panel colors as scene content.
TELEMETRY_RECT_NATIVE = (0, 0, 70, 250)  # (x, y, w, h) in 640-wide space

# Scene family names, matching the ADS file convention.
SCENE_FAMILIES = [
    "ACTIVITY",
    "BUILDING",
    "FISHING",
    "JOHNNY",
    "MARY",
    "MISCGAG",
    "STAND",
    "SUZY",
    "VISITOR",
    "WALKSTUF",
]

# ---------------------------------------------------------------------------
# Palette helpers
# ---------------------------------------------------------------------------

def classify_pixel(r: int, g: int, b: int) -> str:
    """Map an RGB pixel to the nearest palette name, or 'unknown'."""
    # Try exact match first (fastest path for clean captures).
    name = _PALETTE_EXACT.get((r, g, b))
    if name is not None:
        return name

    # Fuzzy match: find nearest palette entry within tolerance.
    best_name = "unknown"
    best_dist = float("inf")
    for pname, (pr, pg, pb) in PALETTE.items():
        dist = abs(r - pr) + abs(g - pg) + abs(b - pb)
        if dist < best_dist:
            best_dist = dist
            best_name = pname
    if best_dist <= _PALETTE_TOL * 3:
        return best_name
    return "unknown"


def _build_palette_map(img: Image.Image, exclude_rect: Optional[Tuple[int, int, int, int]] = None,
                       step: int = 1) -> Dict[str, int]:
    """Count pixels by palette name across the whole image.

    Args:
        img: PIL RGB image.
        exclude_rect: Optional (x, y, w, h) in image coordinates to skip.
        step: Sampling stride (1 = every pixel, 2 = every other, etc.).

    Returns:
        dict mapping palette name -> pixel count.
    """
    px = img.load()
    w, h = img.size
    counts: Dict[str, int] = {}

    ex, ey, ew, eh = exclude_rect if exclude_rect else (-1, -1, 0, 0)

    for y in range(0, h, step):
        for x in range(0, w, step):
            if ex <= x < ex + ew and ey <= y < ey + eh:
                continue
            r, g, b = px[x, y]
            name = classify_pixel(r, g, b)
            counts[name] = counts.get(name, 0) + 1

    return counts


def _region_palette_map(img: Image.Image, rx: int, ry: int, rw: int, rh: int,
                        exclude_rect: Optional[Tuple[int, int, int, int]] = None,
                        step: int = 1) -> Dict[str, int]:
    """Count palette colors in a rectangular region of the image."""
    px = img.load()
    iw, ih = img.size
    counts: Dict[str, int] = {}

    ex, ey, ew, eh = exclude_rect if exclude_rect else (-1, -1, 0, 0)

    x0 = max(0, rx)
    y0 = max(0, ry)
    x1 = min(iw, rx + rw)
    y1 = min(ih, ry + rh)

    for y in range(y0, y1, step):
        for x in range(x0, x1, step):
            if ex <= x < ex + ew and ey <= y < ey + eh:
                continue
            r, g, b = px[x, y]
            name = classify_pixel(r, g, b)
            counts[name] = counts.get(name, 0) + 1

    return counts


# ---------------------------------------------------------------------------
# Screen regions
# ---------------------------------------------------------------------------

@dataclass
class ScreenRegions:
    """Defines the four horizontal bands of the game screen."""
    # All coordinates in image-pixel space.
    width: int
    height: int
    # Telemetry panel exclusion rect (image coords).
    telemetry: Tuple[int, int, int, int]  # (x, y, w, h)
    # Sky band
    sky: Tuple[int, int, int, int]
    # Horizon band
    horizon: Tuple[int, int, int, int]
    # Island / mid-screen band
    island: Tuple[int, int, int, int]
    # Water / bottom band
    water: Tuple[int, int, int, int]


def compute_regions(w: int, h: int) -> ScreenRegions:
    """Compute screen regions for the given image dimensions."""
    # Scale telemetry rect from 640-wide native space.
    sx = w / NATIVE_W
    sy = h / NATIVE_H if h >= 400 else h / 224.0
    tx = int(TELEMETRY_RECT_NATIVE[0] * sx)
    ty = int(TELEMETRY_RECT_NATIVE[1] * sy)
    tw = int(TELEMETRY_RECT_NATIVE[2] * sx)
    th = int(TELEMETRY_RECT_NATIVE[3] * sy)

    sky_top = 0
    sky_bot = int(h * 0.25)
    horizon_top = sky_bot
    horizon_bot = int(h * 0.40)
    island_top = horizon_bot
    island_bot = int(h * 0.72)
    water_top = island_bot
    water_bot = h

    return ScreenRegions(
        width=w,
        height=h,
        telemetry=(tx, ty, tw, th),
        sky=(0, sky_top, w, sky_bot - sky_top),
        horizon=(0, horizon_top, w, horizon_bot - horizon_top),
        island=(0, island_top, w, island_bot - island_top),
        water=(0, water_top, w, water_bot - water_top),
    )


# ---------------------------------------------------------------------------
# Screen type classification
# ---------------------------------------------------------------------------

@dataclass
class ScreenTypeResult:
    screen_type: str   # 'black', 'title', 'ocean', 'island', 'transition'
    confidence: float  # 0.0 .. 1.0
    details: Dict[str, object] = field(default_factory=dict)


def classify_screen_type(img: Image.Image, regions: ScreenRegions) -> ScreenTypeResult:
    """Classify what kind of screen the image shows.

    Returns one of:
        'black'      - Screen is all/mostly black (boot, fade, loading).
        'title'      - Title card is showing (Sierra/Screen Antics splash).
        'ocean'      - Ocean-only view (no island visible).
        'island'     - Island scene (sand, palm, possibly sprites).
        'transition' - Partial fade or mixed state.
    """
    w, h = img.size
    total_pixels = w * h

    # Get full-image palette counts (fast: skip telemetry panel).
    full = _build_palette_map(img, exclude_rect=regions.telemetry, step=2)
    sampled_total = sum(full.values())
    if sampled_total == 0:
        return ScreenTypeResult("black", 1.0)

    def pct(name: str) -> float:
        return full.get(name, 0) * 100.0 / sampled_total

    # --- BLACK / FADE ---
    # A black frame is almost entirely black + near_black.
    black_pct = pct("black") + pct("near_black")
    if black_pct >= 90.0:
        return ScreenTypeResult("black", min(1.0, black_pct / 100.0),
                                {"black_pct": round(black_pct, 1)})

    # --- TITLE SCREEN ---
    # The title screen shows the game scene (island, sky, water) inside a
    # scroll/parchment viewport with a large black border (~60-70% of the
    # full frame).  It also contains the "SCREEN ANTICS!" text and the
    # "SIERRA" logo with dark_magenta pixels.
    #
    # Detection strategy: the title screen is the ONLY state where >40%
    # black co-occurs with significant island/scene colors AND title-logo
    # colors in the lower center. Normal island scenes can also carry large
    # black borders, especially in reference captures, so black+scene alone
    # is not specific enough.
    #
    # Scan for dark_magenta (173,0,173) at step=1 in the lower-center area
    # where the SIERRA logo lives, since step=2 subsampling can miss these
    # sparse pixels.
    high_black = black_pct > 40.0
    scene_colors_pct = (
        pct("cyan") + pct("blue") + pct("dark_blue") + pct("dark_cyan")
        + pct("yellow") + pct("dark_yellow") + pct("green") + pct("dark_green")
        + pct("red") + pct("dark_red")
    )
    if high_black and scene_colors_pct > 8.0:
        # Scan for title-logo color evidence at full resolution.
        dm_count = 0
        px = img.load()
        dm_y0 = int(h * 0.60)
        dm_y1 = int(h * 0.75)
        dm_x0 = int(w * 0.30)
        dm_x1 = int(w * 0.70)
        for y in range(dm_y0, dm_y1):
            for x in range(dm_x0, dm_x1):
                r, g, b = px[x, y]
                if abs(r - 173) < 20 and g < 20 and abs(b - 173) < 20:
                    dm_count += 1
        if dm_count > 100:
            conf = 0.8
            if scene_colors_pct > 15.0:
                conf += 0.1
            if pct("cyan") > 5.0:
                conf += 0.1
            return ScreenTypeResult("title", min(1.0, conf),
                                    {"black_pct": round(black_pct, 1),
                                     "scene_colors_pct": round(scene_colors_pct, 1),
                                     "dark_magenta_found": dm_count})

    # --- OCEAN vs ISLAND ---
    # An ocean scene is dominated by cyan (sky) + blue (water) with no
    # sand/green.  An island scene has yellow/green/gray from the island.
    # Check for island indicator colors.
    sand_pct = pct("yellow") + pct("dark_yellow")
    green_pct = pct("green") + pct("dark_green")
    trunk_pct = pct("dark_red") + pct("red")
    island_signal = sand_pct + green_pct + trunk_pct

    water_sky_pct = pct("cyan") + pct("blue") + pct("dark_blue") + pct("dark_cyan")

    if island_signal > 1.5:
        # Island scene.
        conf = min(1.0, 0.5 + island_signal / 20.0)
        return ScreenTypeResult("island", conf,
                                {"sand_pct": round(sand_pct, 2),
                                 "green_pct": round(green_pct, 2),
                                 "water_sky_pct": round(water_sky_pct, 1)})

    if water_sky_pct > 60.0 and island_signal < 1.0:
        # Ocean only.
        conf = min(1.0, water_sky_pct / 100.0)
        return ScreenTypeResult("ocean", conf,
                                {"water_sky_pct": round(water_sky_pct, 1),
                                 "island_signal": round(island_signal, 2)})

    # --- TRANSITION ---
    # Mix of black + some scene colors during a fade.
    if black_pct > 30.0 and (pct("cyan") > 3.0 or pct("blue") > 3.0):
        return ScreenTypeResult("transition", 0.5,
                                {"black_pct": round(black_pct, 1),
                                 "cyan_pct": round(pct("cyan"), 1)})

    # Fallback: if mostly water/sky with very low island signal, still ocean.
    if water_sky_pct > 40.0:
        return ScreenTypeResult("ocean", 0.4,
                                {"water_sky_pct": round(water_sky_pct, 1)})

    return ScreenTypeResult("transition", 0.3,
                            {"black_pct": round(black_pct, 1),
                             "water_sky_pct": round(water_sky_pct, 1)})


# ---------------------------------------------------------------------------
# Island detection
# ---------------------------------------------------------------------------

@dataclass
class IslandResult:
    present: bool
    confidence: float
    x_center: int = 0
    y_center: int = 0
    sand_pixel_count: int = 0
    green_pixel_count: int = 0


def detect_island(img: Image.Image, regions: ScreenRegions) -> IslandResult:
    """Detect whether the island is visible and estimate its center."""
    # Look for sand (yellow + dark_yellow) and green (palm) in the island band.
    island_band = regions.island
    ix, iy, iw, ih = island_band
    tel = regions.telemetry

    px = img.load()
    w, h = img.size

    sand_xs: List[int] = []
    sand_ys: List[int] = []
    green_xs: List[int] = []
    green_ys: List[int] = []
    sand_count = 0
    green_count = 0

    # Scan the island + some of horizon + some of water band for sand/green.
    scan_y0 = max(0, iy - int(ih * 0.3))
    scan_y1 = min(h, iy + ih + int(ih * 0.4))

    tex, tey, tew, teh = tel

    for y in range(scan_y0, scan_y1, 2):
        for x in range(0, w, 2):
            if tex <= x < tex + tew and tey <= y < tey + teh:
                continue
            r, g, b = px[x, y]
            name = classify_pixel(r, g, b)
            if name in ("yellow", "dark_yellow"):
                sand_xs.append(x)
                sand_ys.append(y)
                sand_count += 1
            elif name in ("green", "dark_green"):
                green_xs.append(x)
                green_ys.append(y)
                green_count += 1

    if sand_count < 10 and green_count < 10:
        return IslandResult(present=False, confidence=0.9)

    # Estimate island center from sand pixels.
    all_xs = sand_xs + green_xs
    all_ys = sand_ys + green_ys
    if all_xs:
        cx = sum(all_xs) // len(all_xs)
        cy = sum(all_ys) // len(all_ys)
    else:
        cx, cy = w // 2, iy + ih // 2

    total_signal = sand_count + green_count
    conf = min(1.0, total_signal / 200.0)

    return IslandResult(
        present=True,
        confidence=conf,
        x_center=cx,
        y_center=cy,
        sand_pixel_count=sand_count * 4,  # compensate for step=2
        green_pixel_count=green_count * 4,
    )


# ---------------------------------------------------------------------------
# Sprite detection
# ---------------------------------------------------------------------------

@dataclass
class SpriteDetection:
    name: str
    present: bool
    confidence: float
    pixel_count: int = 0
    region: Optional[Tuple[int, int, int, int]] = None  # (x, y, w, h) bounding box


def _count_palette_in_region(
    img: Image.Image,
    x0: int, y0: int, x1: int, y1: int,
    target_names: List[str],
    exclude_rect: Optional[Tuple[int, int, int, int]] = None,
    step: int = 2,
) -> Tuple[int, List[int], List[int]]:
    """Count pixels matching target palette names in a region.

    Returns (count, xs, ys) where xs/ys are sample coordinates.
    """
    px = img.load()
    w, h = img.size
    x0 = max(0, x0)
    y0 = max(0, y0)
    x1 = min(w, x1)
    y1 = min(h, y1)

    ex, ey, ew, eh = exclude_rect if exclude_rect else (-1, -1, 0, 0)

    count = 0
    xs: List[int] = []
    ys: List[int] = []
    target_set = set(target_names)

    for y in range(y0, y1, step):
        for x in range(x0, x1, step):
            if ex <= x < ex + ew and ey <= y < ey + eh:
                continue
            r, g, b = px[x, y]
            name = classify_pixel(r, g, b)
            if name in target_set:
                count += 1
                xs.append(x)
                ys.append(y)

    return count, xs, ys


def _bbox(xs: List[int], ys: List[int]) -> Optional[Tuple[int, int, int, int]]:
    """Compute bounding box from coordinate lists."""
    if not xs or not ys:
        return None
    x0, x1 = min(xs), max(xs)
    y0, y1 = min(ys), max(ys)
    return (x0, y0, x1 - x0 + 1, y1 - y0 + 1)


def detect_sprites(img: Image.Image, regions: ScreenRegions) -> List[SpriteDetection]:
    """Detect individual visual elements in the screenshot.

    This detects high-level elements rather than individual animation frames:
    - palm_tree: Green + dark_green in upper-island area.
    - palm_trunk: Dark_red vertical strip in mid-island.
    - sand: Yellow/dark_yellow cluster in island band.
    - raft: Dark_red cluster in lower-island/water (not co-located with trunk).
    - cloud: White/light_gray cluster in sky band.
    - johnny: Red pixels in island band (Johnny's red shirt).
    - water_detail: Near_black (wave shadow) in water band.
    - horizon_line: Dark_cyan in horizon band.
    """
    results: List[SpriteDetection] = []
    w, h = img.size
    tel = regions.telemetry

    # --- PALM TREE CANOPY ---
    # Green colors in upper half of screen (above island center).
    palm_y0 = int(h * 0.15)
    palm_y1 = int(h * 0.50)
    count, xs, ys = _count_palette_in_region(
        img, 0, palm_y0, w, palm_y1, ["green", "dark_green"], exclude_rect=tel)
    count *= 4  # step compensation
    results.append(SpriteDetection(
        name="palm_tree",
        present=count > 100,
        confidence=min(1.0, count / 2000.0),
        pixel_count=count,
        region=_bbox(xs, ys),
    ))

    # --- PALM TRUNK ---
    # The trunk is a narrow vertical column of dark_red pixels ABOVE the sand
    # line, connecting the canopy to the island.  The raft is also dark_red
    # but sits lower and wider.  To isolate the trunk, scan only the region
    # between the canopy bottom and the sand top (roughly 35%-55% of height).
    trunk_y0 = int(h * 0.33)
    trunk_y1 = int(h * 0.58)
    count, xs, ys = _count_palette_in_region(
        img, int(w * 0.25), trunk_y0, int(w * 0.65), trunk_y1,
        ["dark_red"], exclude_rect=tel)
    count *= 4
    # The trunk should have a moderate dark_red count in this region.
    # Even if x-spread is wider than ideal, the presence of dark_red
    # between canopy and sand strongly indicates a trunk.
    trunk_like = count > 30
    results.append(SpriteDetection(
        name="palm_trunk",
        present=trunk_like,
        confidence=min(1.0, count / 800.0) if trunk_like else 0.0,
        pixel_count=count,
        region=_bbox(xs, ys) if trunk_like else None,
    ))

    # --- SAND ---
    sand_y0 = int(h * 0.55)
    sand_y1 = int(h * 0.82)
    count, xs, ys = _count_palette_in_region(
        img, 0, sand_y0, w, sand_y1, ["yellow", "dark_yellow"], exclude_rect=tel)
    count *= 4
    results.append(SpriteDetection(
        name="sand",
        present=count > 100,
        confidence=min(1.0, count / 5000.0),
        pixel_count=count,
        region=_bbox(xs, ys),
    ))

    # --- RAFT ---
    # The raft is a dark_red/red rectangular shape in the water/island band,
    # visually distinct from the trunk (which is narrower and higher).
    # Look for dark_red in the lower half that is NOT co-located with trunk.
    raft_y0 = int(h * 0.55)
    raft_y1 = int(h * 0.80)
    raft_count, rxs, rys = _count_palette_in_region(
        img, int(w * 0.4), raft_y0, w, raft_y1,
        ["dark_red"], exclude_rect=tel)
    raft_count *= 4
    # Raft should have wider x-spread than trunk.
    if rxs:
        raft_x_spread = max(rxs) - min(rxs)
        raft_like = raft_x_spread > 20 and raft_count > 30
    else:
        raft_like = False
    results.append(SpriteDetection(
        name="raft",
        present=raft_like,
        confidence=min(1.0, raft_count / 800.0) if raft_like else 0.0,
        pixel_count=raft_count if raft_like else 0,
        region=_bbox(rxs, rys) if raft_like else None,
    ))

    # --- CLOUDS ---
    sky_band = regions.sky
    sx, sy, sw, sh = sky_band
    count, xs, ys = _count_palette_in_region(
        img, sx, sy, sx + sw, sy + sh,
        ["white", "light_gray"], exclude_rect=tel)
    count *= 4
    results.append(SpriteDetection(
        name="clouds",
        present=count > 100,
        confidence=min(1.0, count / 3000.0),
        pixel_count=count,
        region=_bbox(xs, ys),
    ))

    # --- JOHNNY (red shirt) ---
    # Red pixels in the island/horizon band that are NOT in the sky.
    # Johnny's shirt is bright red; the trunk is dark_red.
    johnny_y0 = int(h * 0.30)
    johnny_y1 = int(h * 0.75)
    count, xs, ys = _count_palette_in_region(
        img, int(w * 0.15), johnny_y0, int(w * 0.85), johnny_y1,
        ["red"], exclude_rect=tel)
    count *= 4
    results.append(SpriteDetection(
        name="johnny",
        present=count > 30,
        confidence=min(1.0, count / 500.0),
        pixel_count=count,
        region=_bbox(xs, ys),
    ))

    # --- WATER DETAIL ---
    water_band = regions.water
    wx, wy, ww, wh = water_band
    count, xs, ys = _count_palette_in_region(
        img, wx, wy, wx + ww, wy + wh,
        ["near_black", "dark_blue"], exclude_rect=tel)
    count *= 4
    results.append(SpriteDetection(
        name="water_detail",
        present=count > 200,
        confidence=min(1.0, count / 5000.0),
        pixel_count=count,
        region=_bbox(xs, ys),
    ))

    # --- HORIZON LINE ---
    hz_band = regions.horizon
    hx, hy, hw, hh = hz_band
    count, xs, ys = _count_palette_in_region(
        img, hx, hy, hx + hw, hy + hh,
        ["dark_cyan"], exclude_rect=tel)
    count *= 4
    results.append(SpriteDetection(
        name="horizon",
        present=count > 50,
        confidence=min(1.0, count / 2000.0),
        pixel_count=count,
        region=_bbox(xs, ys),
    ))

    return results


# ---------------------------------------------------------------------------
# Scene family classification
# ---------------------------------------------------------------------------

@dataclass
class SceneFamilyResult:
    family: str        # e.g. 'STAND', 'FISHING', 'unknown'
    confidence: float
    tag_guess: int = 0
    details: Dict[str, object] = field(default_factory=dict)


# Scene families and their characteristic BMP sprite colors / layout hints.
# This is heuristic: without telemetry bar data we can only make rough guesses
# based on what visual elements are present.
#
# Key observations from scene spec data:
#   STAND:      Johnny idle on island, minimal sprites.  BMPs: MJ_AMB, MJTELE.
#   JOHNNY:     Story scenes.  BMPs: MEANWHIL (text), SJWORK, THEEND.
#   FISHING:    Johnny fishing.  BMPs: MJFISH, LILFISH, SPLASH, FISHMAN.
#   ACTIVITY:   Various activities (diving, reading, bathing).
#               BMPs: MJDIVE, MJREAD, MJBATH, COCONUTS, ZZZZS.
#   BUILDING:   Sand castles, fire.  BMPs: MJSAND, MJFIRE.
#   VISITOR:    Ships/visitors.  BMPs: SHIPS, TANKER, GJCASTLE.
#   MARY:       Mary character.  BMPs: SASKDATE, SBREAKUP.
#   WALKSTUF:   Walking/jogging.  BMPs: MJJOG, MJRAFT, WOULDBE.
#   MISCGAG:    Gull, shark, hot.  BMPs: GJGULL, SHARK, GJHOT.
#   SUZY:       City skyline scenes.  BMPs: SUZYCITY.
#
# For visual-only detection without BMP metadata, we can distinguish:
# - OCEAN scenes (walking, transitional) vs ISLAND scenes
# - Presence of specific elements (raft, fish, ships, etc.)
# But most family classification will have low confidence without telemetry.

def detect_scene_family(
    screen_type: ScreenTypeResult,
    island_result: IslandResult,
    sprites: List[SpriteDetection],
) -> SceneFamilyResult:
    """Estimate which scene family is active based on visual analysis.

    Without telemetry data this is necessarily approximate.  The telemetry
    bars (ads_scene_sig, story_scene_tag) provide much more precise data.
    """
    if screen_type.screen_type == "black":
        return SceneFamilyResult("unknown", 0.0, details={"reason": "black_screen"})

    if screen_type.screen_type == "title":
        return SceneFamilyResult("TITLE", 0.9, details={"reason": "title_screen_detected"})

    if screen_type.screen_type == "ocean":
        # Ocean-only could be WALKSTUF (walking transition) or early boot.
        return SceneFamilyResult("WALKSTUF", 0.3,
                                 details={"reason": "ocean_only_likely_walk_transition"})

    # Island scene -- try to narrow down.
    sprite_map = {s.name: s for s in sprites}

    has_johnny = sprite_map.get("johnny", SpriteDetection("", False, 0.0)).present
    has_raft = sprite_map.get("raft", SpriteDetection("", False, 0.0)).present
    has_palm = sprite_map.get("palm_tree", SpriteDetection("", False, 0.0)).present
    has_sand = sprite_map.get("sand", SpriteDetection("", False, 0.0)).present

    details: Dict[str, object] = {
        "has_johnny": has_johnny,
        "has_raft": has_raft,
        "has_palm": has_palm,
        "has_sand": has_sand,
    }

    if not has_sand and not has_palm:
        return SceneFamilyResult("unknown", 0.2, details=details)

    # With island visible but no Johnny, could be a transitional frame.
    if not has_johnny:
        if has_raft:
            return SceneFamilyResult("WALKSTUF", 0.3, details=details)
        return SceneFamilyResult("STAND", 0.2,
                                 details={**details, "reason": "island_no_johnny"})

    # Johnny is present on island.
    # STAND is the most common family when Johnny is just standing there.
    # Without more visual cues we default to STAND with moderate confidence.
    if has_raft:
        # Raft visible + Johnny often indicates WALKSTUF or FISHING.
        return SceneFamilyResult("FISHING", 0.3,
                                 details={**details, "reason": "johnny_with_raft"})

    # Default: STAND (most common idle scene).
    return SceneFamilyResult("STAND", 0.3,
                             details={**details, "reason": "johnny_on_island_default"})


# ---------------------------------------------------------------------------
# Telemetry integration (optional)
# ---------------------------------------------------------------------------

def _try_read_telemetry(img_path: str) -> Optional[Dict[str, object]]:
    """Attempt to read telemetry bars from the same screenshot.

    This calls the decode-ps1-bars module if available, providing much
    more precise scene identification.
    """
    try:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        bars_module = os.path.join(script_dir, "decode-ps1-bars.py")
        if not os.path.exists(bars_module):
            return None

        import importlib.util
        spec = importlib.util.spec_from_file_location("decode_ps1_bars", bars_module)
        if spec is None or spec.loader is None:
            return None
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        result = mod.decode_file(img_path, x0=4, scan_width=90, include_zero=False)
        return result.get("interpreted")
    except Exception:
        return None


def _enhance_family_from_telemetry(
    family_result: SceneFamilyResult,
    telemetry: Dict[str, object],
) -> SceneFamilyResult:
    """Refine scene family classification using telemetry data."""
    ads_info = telemetry.get("ads")
    story_info = telemetry.get("story")

    if story_info:
        scene_tag = story_info.get("scene_tag", 0)
        ads_sig = story_info.get("ads_sig", 0)
        seq = story_info.get("seq", 0)
        if scene_tag > 0:
            family_result.details["telemetry_scene_tag"] = scene_tag
            family_result.details["telemetry_ads_sig"] = ads_sig
            family_result.tag_guess = scene_tag

    if ads_info:
        scene_sig = ads_info.get("scene_sig", 0)
        slot_est = ads_info.get("scene_slot_estimate", 0)
        tag_est = ads_info.get("scene_tag_estimate", 0)
        if scene_sig > 0:
            family_result.details["telemetry_scene_sig"] = scene_sig
            family_result.details["telemetry_slot_estimate"] = slot_est
            family_result.details["telemetry_tag_estimate"] = tag_est
            # Slot maps to ADS family (from story.c):
            #   1=ACTIVITY, 2=BUILDING, 3=FISHING, 4=JOHNNY, 5=MARY,
            #   6=MISCGAG, 7=STAND, 9=VISITOR, 10=WALKSTUF
            SLOT_TO_FAMILY = {
                1: "ACTIVITY", 2: "BUILDING", 3: "FISHING", 4: "JOHNNY",
                5: "MARY", 6: "MISCGAG", 7: "STAND", 9: "VISITOR",
                10: "WALKSTUF",
            }
            if slot_est in SLOT_TO_FAMILY:
                family_result.family = SLOT_TO_FAMILY[slot_est]
                family_result.confidence = 0.8
                family_result.tag_guess = tag_est

    return family_result


# ---------------------------------------------------------------------------
# Full analysis
# ---------------------------------------------------------------------------

@dataclass
class FullAnalysis:
    file: str
    image_size: Tuple[int, int]
    screen_type: ScreenTypeResult
    island: IslandResult
    sprites: List[SpriteDetection]
    scene_family: SceneFamilyResult
    telemetry_available: bool = False

    def to_dict(self) -> Dict[str, object]:
        """Convert to JSON-serializable dictionary."""
        return {
            "file": self.file,
            "image_size": {"width": self.image_size[0], "height": self.image_size[1]},
            "screen_type": {
                "type": self.screen_type.screen_type,
                "confidence": round(self.screen_type.confidence, 3),
                "details": self.screen_type.details,
            },
            "island": {
                "present": self.island.present,
                "confidence": round(self.island.confidence, 3),
                "x_center": self.island.x_center,
                "y_center": self.island.y_center,
                "sand_pixel_count": self.island.sand_pixel_count,
                "green_pixel_count": self.island.green_pixel_count,
            },
            "sprites": [
                {
                    "name": s.name,
                    "present": s.present,
                    "confidence": round(s.confidence, 3),
                    "pixel_count": s.pixel_count,
                    "region": (
                        {"x": s.region[0], "y": s.region[1],
                         "w": s.region[2], "h": s.region[3]}
                        if s.region else None
                    ),
                }
                for s in self.sprites
            ],
            "scene_family": {
                "family": self.scene_family.family,
                "confidence": round(self.scene_family.confidence, 3),
                "tag_guess": self.scene_family.tag_guess,
                "details": self.scene_family.details,
            },
            "telemetry_available": self.telemetry_available,
        }


class ScreenAnalyzer:
    """Analyzes a single screenshot."""

    def __init__(self, image_path: str):
        self.path = image_path
        self.img = Image.open(image_path).convert("RGB")
        self.width, self.height = self.img.size
        self.regions = compute_regions(self.width, self.height)

    def classify_screen_type(self) -> ScreenTypeResult:
        """Returns screen type classification with confidence."""
        return classify_screen_type(self.img, self.regions)

    def detect_island(self) -> IslandResult:
        """Returns island detection result."""
        return detect_island(self.img, self.regions)

    def detect_sprites(self) -> List[SpriteDetection]:
        """Returns list of sprite detection results."""
        return detect_sprites(self.img, self.regions)

    def detect_scene_family(self) -> SceneFamilyResult:
        """Returns scene family classification."""
        st = self.classify_screen_type()
        il = self.detect_island()
        sp = self.detect_sprites()
        return detect_scene_family(st, il, sp)

    def full_analysis(self) -> FullAnalysis:
        """Returns complete analysis."""
        st = self.classify_screen_type()
        il = self.detect_island()
        sp = self.detect_sprites()
        sf = detect_scene_family(st, il, sp)

        # Try telemetry enhancement.
        telemetry = _try_read_telemetry(self.path)
        telemetry_available = telemetry is not None and bool(telemetry)
        if telemetry_available:
            sf = _enhance_family_from_telemetry(sf, telemetry)

        return FullAnalysis(
            file=self.path,
            image_size=(self.width, self.height),
            screen_type=st,
            island=il,
            sprites=sp,
            scene_family=sf,
            telemetry_available=telemetry_available,
        )


# ---------------------------------------------------------------------------
# Validation helper (for regtest harness integration)
# ---------------------------------------------------------------------------

@dataclass
class ValidationResult:
    passed: bool
    missing_sprites: List[str]
    unexpected_sprites: List[str]
    screen_type_match: bool
    family_match: bool
    details: Dict[str, object] = field(default_factory=dict)


def validate_scene(
    frame_path: str,
    expected_scene: Optional[str] = None,
    expected_sprites: Optional[List[str]] = None,
    expected_screen_type: Optional[str] = None,
) -> ValidationResult:
    """Validate a screenshot against expected scene characteristics.

    Args:
        frame_path: Path to the screenshot PNG.
        expected_scene: Expected scene family string, e.g. "STAND 2" or "FISHING".
                        Format: "FAMILY" or "FAMILY TAG".
        expected_sprites: List of sprite names expected to be present.
        expected_screen_type: Expected screen type ('island', 'ocean', etc.).

    Returns:
        ValidationResult with pass/fail and details.

    Example:
        result = validate_scene("frame.png", "STAND 2", ["johnny", "palm_tree"])
    """
    analyzer = ScreenAnalyzer(frame_path)
    analysis = analyzer.full_analysis()

    missing: List[str] = []
    unexpected: List[str] = []
    screen_match = True
    family_match = True

    # Check screen type.
    if expected_screen_type:
        screen_match = analysis.screen_type.screen_type == expected_screen_type

    # Check scene family.
    if expected_scene:
        parts = expected_scene.strip().split()
        expected_family = parts[0].upper()
        expected_tag = int(parts[1]) if len(parts) > 1 else 0
        family_match = analysis.scene_family.family == expected_family
        if expected_tag > 0 and analysis.scene_family.tag_guess > 0:
            family_match = family_match and (analysis.scene_family.tag_guess == expected_tag)

    # Check sprites.
    if expected_sprites:
        detected_names = {s.name for s in analysis.sprites if s.present}
        for sp in expected_sprites:
            if sp not in detected_names:
                missing.append(sp)
        # Note: we don't flag "unexpected" sprites aggressively, since many
        # elements (clouds, horizon, water_detail) are always present.

    passed = screen_match and family_match and len(missing) == 0

    return ValidationResult(
        passed=passed,
        missing_sprites=missing,
        unexpected_sprites=unexpected,
        screen_type_match=screen_match,
        family_match=family_match,
        details={
            "analysis": analysis.to_dict(),
            "expected_scene": expected_scene,
            "expected_sprites": expected_sprites,
            "expected_screen_type": expected_screen_type,
        },
    )


# ---------------------------------------------------------------------------
# Batch analysis
# ---------------------------------------------------------------------------

def analyze_screenshot(path: str, json_output: bool = False) -> FullAnalysis:
    """Main entry point for single screenshot analysis."""
    analyzer = ScreenAnalyzer(path)
    return analyzer.full_analysis()


def analyze_batch(directory: str, json_output: bool = False) -> List[FullAnalysis]:
    """Analyze all PNGs in a directory, sorted by filename."""
    results: List[FullAnalysis] = []
    files = sorted(
        f for f in os.listdir(directory) if f.lower().endswith(".png")
    )
    for fn in files:
        path = os.path.join(directory, fn)
        try:
            analysis = analyze_screenshot(path, json_output=json_output)
            results.append(analysis)
        except Exception as e:
            print(f"warning: failed to analyze {path}: {e}", file=sys.stderr)
    return results


# ---------------------------------------------------------------------------
# Human-readable output
# ---------------------------------------------------------------------------

def _format_human(analysis: FullAnalysis) -> str:
    """Format analysis as a concise human-readable string."""
    lines: List[str] = []
    st = analysis.screen_type
    il = analysis.island
    sf = analysis.scene_family

    # Header line.
    lines.append(f"file: {analysis.file}  ({analysis.image_size[0]}x{analysis.image_size[1]})")

    # Screen type.
    lines.append(
        f"  Screen: {st.screen_type} (confidence={st.confidence:.2f})"
    )
    if st.details:
        detail_str = ", ".join(f"{k}={v}" for k, v in st.details.items())
        lines.append(f"    details: {detail_str}")

    # Island.
    if il.present:
        lines.append(
            f"  Island: present at ({il.x_center}, {il.y_center}) "
            f"(confidence={il.confidence:.2f}, "
            f"sand={il.sand_pixel_count}px, green={il.green_pixel_count}px)"
        )
    else:
        lines.append(f"  Island: not present (confidence={il.confidence:.2f})")

    # Sprites.
    present_sprites = [s for s in analysis.sprites if s.present]
    absent_sprites = [s for s in analysis.sprites if not s.present]
    if present_sprites:
        sprite_strs = []
        for s in present_sprites:
            region_str = ""
            if s.region:
                region_str = f" @({s.region[0]},{s.region[1]} {s.region[2]}x{s.region[3]})"
                sprite_strs.append(f"{s.name}({s.confidence:.2f}, {s.pixel_count}px{region_str})")
        lines.append(f"  Sprites: {', '.join(sprite_strs)}")
    if absent_sprites:
        lines.append(f"  Absent: {', '.join(s.name for s in absent_sprites)}")

    # Scene family.
    lines.append(
        f"  Scene: {sf.family}"
        + (f" tag={sf.tag_guess}" if sf.tag_guess > 0 else "")
        + f" (confidence={sf.confidence:.2f})"
    )
    if sf.details:
        detail_str = ", ".join(f"{k}={v}" for k, v in sf.details.items())
        lines.append(f"    details: {detail_str}")

    if analysis.telemetry_available:
        lines.append("  Telemetry: available (used for scene refinement)")

    return "\n".join(lines)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(
        description="Visual scene and sprite detection for Johnny Castaway PS1 port."
    )
    ap.add_argument("images", nargs="*", help="Path(s) to screenshot PNG files")
    ap.add_argument("--json", action="store_true", help="Output JSON instead of human-readable text")
    ap.add_argument("--batch", metavar="DIR", help="Analyze all PNGs in DIR")
    ap.add_argument("--expect", metavar="SCENE",
                    help="Validate against expected scene (e.g. 'STAND 2')")
    ap.add_argument("--expect-sprites", metavar="SPRITES",
                    help="Comma-separated list of expected sprite names")
    ap.add_argument("--expect-screen", metavar="TYPE",
                    help="Expected screen type (black, title, ocean, island, transition)")
    args = ap.parse_args()

    if not args.images and not args.batch:
        ap.error("provide image paths or --batch DIR")

    results: List[FullAnalysis] = []

    # Batch mode.
    if args.batch:
        if not os.path.isdir(args.batch):
            print(f"error: {args.batch} is not a directory", file=sys.stderr)
            return 1
        results = analyze_batch(args.batch, json_output=args.json)

    # Individual files.
    for path in (args.images or []):
        if not os.path.isfile(path):
            print(f"error: {path} not found", file=sys.stderr)
            return 1
        results.append(analyze_screenshot(path, json_output=args.json))

    # Validation mode.
    if args.expect and len(results) == 1:
        expected_sprites = None
        if args.expect_sprites:
            expected_sprites = [s.strip() for s in args.expect_sprites.split(",")]
        vr = validate_scene(
            results[0].file,
            expected_scene=args.expect,
            expected_sprites=expected_sprites,
            expected_screen_type=args.expect_screen,
        )
        if args.json:
            out = {
                "passed": vr.passed,
                "missing_sprites": vr.missing_sprites,
                "unexpected_sprites": vr.unexpected_sprites,
                "screen_type_match": vr.screen_type_match,
                "family_match": vr.family_match,
            }
            print(json.dumps(out, indent=2))
        else:
            status = "PASS" if vr.passed else "FAIL"
            print(f"Validation: {status}")
            if vr.missing_sprites:
                print(f"  Missing sprites: {', '.join(vr.missing_sprites)}")
            if not vr.screen_type_match:
                print(f"  Screen type mismatch")
            if not vr.family_match:
                print(f"  Family mismatch")
        return 0 if vr.passed else 1

    # Output.
    if args.json:
        data = [r.to_dict() for r in results]
        print(json.dumps(data if len(data) > 1 else data[0], indent=2))
    else:
        for i, analysis in enumerate(results):
            if i:
                print()
            print(_format_human(analysis))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
