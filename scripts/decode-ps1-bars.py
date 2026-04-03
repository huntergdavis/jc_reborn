#!/usr/bin/env python3
"""Decode PS1 left debug panel bars from a screenshot.

Usage:
  scripts/decode-ps1-bars.py <screenshot.png>
  scripts/decode-ps1-bars.py --json <screenshot.png>
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from statistics import median
from typing import Dict, List, Optional, Tuple

try:
    from PIL import Image, ImageChops
except Exception:
    print("error: Pillow is required (pip install pillow)", file=sys.stderr)
    raise

# Native PS1 framebuffer dimensions (640x480 interlaced)
NATIVE_W = 640
NATIVE_H = 480
HEADLESS_NATIVE_H = 448

ACTOR_PANEL_X = 560
ACTOR_PANEL_Y = 140
ACTOR_PANEL_DATA_X = ACTOR_PANEL_X + 8
ACTOR_PANEL_SCAN_W = 63
ACTOR_PANEL_ENTITY_STRIDE = 12
ACTOR_PANEL_ROW_OFFSETS = {
    "centroid_x": 0,
    "centroid_y": 3,
    "bbox_width": 6,
    "bbox_height": 9,
}
ACTOR_PANEL_ENTITIES = ("johnny", "mary", "suzy", "other_actor")
ACTOR_PANEL_FAMILIES = {
    "centroid_x": "cyan",
    "centroid_y": "green",
    "bbox_width": "magenta",
    "bbox_height": "yellow",
}


@dataclass(frozen=True)
class RowDef:
    key: str
    y: int
    panel: str
    desc: str
    decode_hint: str = ""
    family: str = "lit"


@dataclass(frozen=True)
class PanelDef:
    key: str
    base_y: int
    height: int


@dataclass(frozen=True)
class PanelBand:
    panel: str
    top: int
    bottom: int
    left: int
    right: int


ROWS: List[RowDef] = [
    RowDef("drop_thread_drops", 4, "drop", "gStatThreadDrops", family="red"),
    RowDef("drop_bmp_frame_cap", 10, "drop", "gStatBmpFrameCapHits", family="magenta"),
    RowDef("drop_bmp_short_load", 13, "drop", "gStatBmpShortLoads", family="yellow"),

    RowDef("ads_active_threads", 91, "ads", "ps1AdsDbgActiveThreads", "value ~= width", "cyan"),
    RowDef("ads_mini", 94, "ads", "ps1AdsDbgMini", "value ~= width", "white"),
    RowDef("ads_scene_sig", 97, "ads", "((sceneSlot & 0x7) << 3) | (sceneTag & 0x7)", "value ~= width", "yellow"),
    RowDef("ads_replay_count", 100, "ads", "ps1AdsDbgReplayCount", "value ~= width", "magenta"),
    RowDef("ads_running_threads", 103, "ads", "ps1AdsDbgRunningThreads", "value ~= width", "cyan"),
    RowDef("ads_update_delay", 106, "ads", "grUpdateDelay", "value ~= width", "green"),
    RowDef("ads_replay_try_frame", 109, "ads", "ps1AdsDbgReplayTryFrame", "value ~= width", "white"),
    RowDef("ads_replay_draw_frame", 112, "ads", "ps1AdsDbgReplayDrawFrame", "value ~= width", "green"),
    RowDef("ads_merge_carry_frame", 115, "ads", "ps1AdsDbgMergeCarryFrame", "value ~= width", "magenta"),
    RowDef("ads_no_draw_threads_frame", 118, "ads", "ps1AdsDbgNoDrawThreadsFrame", "value ~= width", "red"),
    RowDef("ads_played_threads_frame", 121, "ads", "ps1AdsDbgPlayedThreadsFrame", "value ~= width", "cyan"),
    RowDef("ads_recorded_sprites_frame", 124, "ads", "ps1AdsDbgRecordedSpritesFrame", "value ~= width", "yellow"),
    RowDef("ads_terminated_threads", 127, "ads", "ps1AdsDbgTerminatedThreads", "value ~= width", "red"),
    RowDef("ads_last_bmp_slot", 130, "ads", "last grLoadBmpRAM slot (+1)", "value ~= width", "white"),
    RowDef("ads_last_bmp_frames", 133, "ads", "last grLoadBmpRAM frame count", "value ~= width", "green"),
    RowDef("ads_last_bmp_status", 136, "ads", "last grLoadBmpRAM status", "value ~= width", "magenta"),

    RowDef("mem_used_pct_scaled", 175, "mem", "(used/budget)*63", "used_pct ~= width * 100 / 63", "green"),
    RowDef("mem_loaded_bmp", 178, "mem", "gStatLoadedBmp", "value ~= width", "cyan"),
    RowDef("mem_loaded_ttm", 181, "mem", "gStatLoadedTtm", "value ~= width", "magenta"),
    RowDef("mem_loaded_ads", 184, "mem", "gStatLoadedAds", "value ~= width", "red"),
    RowDef("mem_used_16k", 187, "mem", "used_bytes >> 14", "used_bytes ~= width * 16384", "white"),
    RowDef("mem_budget_16k", 190, "mem", "budget_bytes >> 14", "budget_bytes ~= width * 16384", "gray"),

    RowDef("story_seq", 223, "story", "ps1StoryDbgSeq", "value ~= width", "gray"),
    RowDef("story_phase", 226, "story", "ps1StoryDbgPhase", "value ~= width", "white"),
    RowDef("story_scene_tag", 229, "story", "ps1StoryDbgSceneTag", "value ~= width", "green"),
    RowDef("story_ads_sig", 232, "story", "hash(adsName)", "value ~= width", "magenta"),
    RowDef("story_prev_sig", 235, "story", "(prevSpot*8)+prevHdg", "value ~= width", "cyan"),
    RowDef("story_next_sig", 238, "story", "(nextSpot*8)+nextHdg", "value ~= width", "yellow"),
    RowDef("pilot_pack_active", 31, "pilotpack", "ps1PilotDbgActivePack", "value ~= width", "white"),
    RowDef("pilot_pack_hits", 36, "pilotpack", "ps1PilotDbgHits", "value ~= width", "green"),
    RowDef("pilot_pack_fallbacks", 41, "pilotpack", "ps1PilotDbgFallbacks", "value ~= width", "red"),
    RowDef("pilot_pack_last_hit_entry", 46, "pilotpack", "ps1PilotDbgLastHitEntry", "value ~= width", "yellow"),
    RowDef("pilot_pack_last_fallback_entry", 51, "pilotpack", "ps1PilotDbgLastFallbackEntry", "value ~= width", "magenta"),
    RowDef("pilot_pack_active_misses", 56, "pilotpack", "ps1PilotDbgFallbackWhilePackActive", "value ~= width", "cyan"),
]

PANELS: List[PanelDef] = [
    PanelDef("drop", 2, 16),
    PanelDef("pilotpack", 30, 30),
    PanelDef("ads", 90, 50),
    PanelDef("mem", 174, 21),
    PanelDef("story", 222, 21),
]
PANEL_BY_KEY = {panel.key: panel for panel in PANELS}
ROW_BY_KEY = {row.key: row for row in ROWS}


def count_non_black_row(rgb_img: Image.Image, y: int, x0: int, width: int) -> int:
    px = rgb_img.load()
    x1 = min(rgb_img.width, x0 + width)
    yy = max(0, min(rgb_img.height - 1, y))
    # DuckStation scaling/filtering can turn black into near-black; treat those as black.
    # Bars are saturated colors, so require a minimum channel intensity.
    started = False
    lit_count = 0
    dark_run = 0
    for x in range(x0, x1):
        r, g, b = px[x, yy]
        is_lit = (max(r, g, b) >= 24)
        if is_lit:
            if not started:
                started = True
            lit_count += 1
            dark_run = 0
        elif started:
            dark_run += 1
            # End bar after a small run of black pixels to reject isolated noise.
            if dark_run >= 3:
                break
    return lit_count


def pixel_matches_family(rgb: Tuple[int, int, int], family: str) -> bool:
    r, g, b = rgb
    mx = max(r, g, b)
    mn = min(r, g, b)
    if mx < 24:
        return False
    if family == "white":
        return mx >= 96 and (mx - mn) <= 48
    if family == "gray":
        return mx >= 40 and (mx - mn) <= 24
    if family == "red":
        return r >= 64 and r >= g + 24 and r >= b + 24
    if family == "green":
        return g >= 64 and g >= r + 24 and g >= b + 24
    if family == "blue":
        return b >= 64 and b >= r + 24 and b >= g + 24
    if family == "yellow":
        return r >= 48 and g >= 48 and b <= min(r, g) * 0.55
    if family == "magenta":
        return r >= 48 and b >= 48 and g <= min(r, b) * 0.55
    if family == "cyan":
        return g >= 48 and b >= 48 and r <= min(g, b) * 0.55
    return True


def count_family_row(rgb_img: Image.Image, y: int, x0: int, width: int, family: str) -> int:
    px = rgb_img.load()
    x1 = min(rgb_img.width, x0 + width)
    yy = max(0, min(rgb_img.height - 1, y))
    started = False
    lit_count = 0
    dark_run = 0
    for x in range(x0, x1):
        is_lit = pixel_matches_family(px[x, yy], family)
        if is_lit:
            started = True
            lit_count += 1
            dark_run = 0
        elif started:
            dark_run += 1
            if dark_run >= 3:
                break
    return lit_count


def find_black_runs(
    rgb_img: Image.Image, y: int, x_start: int, x_end: int, min_level: int = 10
) -> List[Tuple[int, int]]:
    px = rgb_img.load()
    runs: List[Tuple[int, int]] = []
    active = False
    start = x_start
    yy = max(0, min(rgb_img.height - 1, y))

    for x in range(max(0, x_start), min(rgb_img.width, x_end + 1)):
        r, g, b = px[x, yy]
        is_black = max(r, g, b) <= min_level
        if is_black and not active:
            start = x
            active = True
        elif not is_black and active:
            runs.append((start, x - 1))
            active = False

    if active:
        runs.append((start, min(rgb_img.width - 1, x_end)))

    return runs


def longest_black_run(
    rgb_img: Image.Image, y: int, x_start: int, x_end: int, min_level: int = 10
) -> Optional[Tuple[int, int]]:
    runs = find_black_runs(rgb_img, y, x_start, x_end, min_level=min_level)
    if not runs:
        return None
    return max(runs, key=lambda run: run[1] - run[0] + 1)


def find_viewport_box(rgb_img: Image.Image) -> Tuple[int, int, int, int]:
    # When fallback captures grab the DuckStation window instead of a native
    # emulator screenshot, crop to the colorful 4:3 game viewport first.
    step = 2 if max(rgb_img.width, rgb_img.height) > 1000 else 1
    xs: List[int] = []
    ys: List[int] = []
    px = rgb_img.load()

    for y in range(0, rgb_img.height, step):
        for x in range(0, rgb_img.width, step):
            r, g, b = px[x, y]
            chroma = max(r, g, b) - min(r, g, b)
            if chroma >= 28 and max(r, g, b) >= 48:
                xs.append(x)
                ys.append(y)

    if not xs or not ys:
        return (0, 0, rgb_img.width, rgb_img.height)

    left = max(0, min(xs) - 12)
    top = max(0, min(ys) - 12)
    right = min(rgb_img.width, max(xs) + 13)
    bottom = min(rgb_img.height, max(ys) + 13)

    box_w = right - left
    box_h = bottom - top
    if box_w <= 0 or box_h <= 0:
        return (0, 0, rgb_img.width, rgb_img.height)

    target_w = max(box_w, int(round(box_h * (4.0 / 3.0))))
    target_h = max(box_h, int(round(target_w * (3.0 / 4.0))))
    cx = (left + right) // 2
    cy = (top + bottom) // 2

    left = cx - (target_w // 2)
    top = cy - (target_h // 2)
    right = left + target_w
    bottom = top + target_h

    if left < 0:
        right -= left
        left = 0
    if top < 0:
        bottom -= top
        top = 0
    if right > rgb_img.width:
        shift = right - rgb_img.width
        left = max(0, left - shift)
        right = rgb_img.width
    if bottom > rgb_img.height:
        shift = bottom - rgb_img.height
        top = max(0, top - shift)
        bottom = rgb_img.height

    return (left, top, right, bottom)


def find_panel_bands(
    rgb_img: Image.Image, viewport_box: Tuple[int, int, int, int]
) -> Dict[str, PanelBand]:
    viewport_left, _, viewport_right, _ = viewport_box
    search_left = max(0, viewport_left - 64)
    search_right = min(rgb_img.width - 1, viewport_right)
    rows: List[Tuple[int, int, int]] = []
    active = False
    band_start = 0
    starts: List[int] = []
    ends: List[int] = []

    for y in range(rgb_img.height):
        run = longest_black_run(rgb_img, y, search_left, search_right, min_level=10)
        run_len = 0 if run is None else (run[1] - run[0] + 1)
        is_band_row = run is not None and run_len >= 120

        if is_band_row:
            if not active:
                band_start = y
                starts = []
                ends = []
                active = True
            starts.append(run[0])
            ends.append(run[1])
        elif active:
            rows.append((band_start, y - 1, int(round(median(starts))), int(round(median(ends)))))
            active = False

    if active:
        rows.append((band_start, rgb_img.height - 1, int(round(median(starts))), int(round(median(ends)))))

    if not rows:
        return {}

    # Drop-panel bars can consume nearly the whole box, so exact black-run detection
    # only catches its top edge. Anchor it from the next panel and the known native gap.
    bands: Dict[str, PanelBand] = {}
    usable = rows[:]

    if usable:
        first = usable[0]
        if len(usable) >= 2:
            second = usable[1]
            approx_drop_bottom = max(first[1], second[0] - 8)
            drop_right = max(first[3], second[3])
            bands["drop"] = PanelBand("drop", 0, approx_drop_bottom, first[2], drop_right)
        else:
            bands["drop"] = PanelBand("drop", first[0], first[1], first[2], first[3])

    remaining = usable[1:] if len(usable) > 1 else []
    order = ["pilotpack", "ads", "mem", "story"]
    for panel, row in zip(order, remaining):
        bands[panel] = PanelBand(panel, row[0], row[1], row[2], row[3])

    return bands


def build_interpreted(rows: List[Dict[str, object]]) -> Dict[str, object]:
    row_map = {str(row["key"]): int(row["width"]) for row in rows}
    interpreted: Dict[str, object] = {}

    if "pilot_pack_active" in row_map or "pilot_pack_hits" in row_map or "pilot_pack_fallbacks" in row_map:
        active = row_map.get("pilot_pack_active", 0)
        hits = row_map.get("pilot_pack_hits", 0)
        fallbacks = row_map.get("pilot_pack_fallbacks", 0)
        interpreted["pilot_pack"] = {
            "active_pack_id": active,
            "hits": hits,
            "fallbacks": fallbacks,
            "last_hit_entry": row_map.get("pilot_pack_last_hit_entry", 0),
            "last_fallback_entry": row_map.get("pilot_pack_last_fallback_entry", 0),
            "active_pack_misses": row_map.get("pilot_pack_active_misses", 0),
            "pack_path_used": hits > 0,
            "fallback_observed": fallbacks > 0,
        }

    if "mem_used_pct_scaled" in row_map or "mem_used_16k" in row_map or "mem_budget_16k" in row_map:
        used_pct_scaled = row_map.get("mem_used_pct_scaled", 0)
        used_16k = row_map.get("mem_used_16k", 0)
        budget_16k = row_map.get("mem_budget_16k", 0)
        interpreted["memory"] = {
            "used_pct_estimate": int(round((used_pct_scaled * 100.0) / 63.0)) if used_pct_scaled > 0 else 0,
            "used_bytes_estimate": used_16k * 16384,
            "budget_bytes_estimate": budget_16k * 16384,
        }

    if "ads_scene_sig" in row_map:
        sig = row_map["ads_scene_sig"]
        interpreted["ads"] = {
            "scene_sig": sig,
            "scene_slot_estimate": (sig >> 3) & 0x7,
            "scene_tag_estimate": sig & 0x7,
            "active_threads": row_map.get("ads_active_threads", 0),
            "running_threads": row_map.get("ads_running_threads", 0),
            "replay_count": row_map.get("ads_replay_count", 0),
            "last_bmp_slot": row_map.get("ads_last_bmp_slot", 0),
            "last_bmp_frames": row_map.get("ads_last_bmp_frames", 0),
            "last_bmp_status": row_map.get("ads_last_bmp_status", 0),
        }

    if "story_prev_sig" in row_map or "story_next_sig" in row_map:
        prev_sig = row_map.get("story_prev_sig", 0)
        next_sig = row_map.get("story_next_sig", 0)
        interpreted["story"] = {
            "seq": row_map.get("story_seq", 0),
            "phase": row_map.get("story_phase", 0),
            "scene_tag": row_map.get("story_scene_tag", 0),
            "ads_sig": row_map.get("story_ads_sig", 0),
            "prev_sig": prev_sig,
            "prev_spot_estimate": prev_sig >> 3,
            "prev_hdg_estimate": prev_sig & 0x7,
            "next_sig": next_sig,
            "next_spot_estimate": next_sig >> 3,
            "next_hdg_estimate": next_sig & 0x7,
        }

    return interpreted


def decode_file(path: str, x0: int, scan_width: int, include_zero: bool) -> Dict[str, object]:
    img = Image.open(path).convert("RGB")
    viewport_box = find_viewport_box(img)
    panel_bands = find_panel_bands(img, viewport_box)

    # Scale VRAM coordinates to screenshot coordinates.
    # Use the colorful 4:3 viewport to estimate horizontal scaling, but keep
    # sampling in absolute screenshot space because the left telemetry panels can
    # live inside pillarbox space rather than the cropped viewport.
    scale_x = (viewport_box[2] - viewport_box[0]) / NATIVE_W
    scale_y = (viewport_box[3] - viewport_box[1]) / NATIVE_H

    rows = []
    for row in ROWS:
        panel_band = panel_bands.get(row.panel)
        panel_def = PANEL_BY_KEY.get(row.panel)
        if panel_band and panel_def:
            panel_scale_y = max(
                1.0,
                (panel_band.bottom - panel_band.top + 1) / float(panel_def.height),
            )
            panel_left = panel_band.left
            abs_x0 = max(0, panel_left + int(round(max(0, x0 - 2) * scale_x)))
            abs_width = max(1, int(round(scan_width * scale_x)))
            rel_y = row.y - panel_def.base_y + 1
            center_y = panel_band.top + int(round(rel_y * panel_scale_y))
            best_w = 0
            best_family_w = 0
            best_lit_w = 0
            for y_off in (-2, -1, 0, 1, 2):
                sample_y = max(panel_band.top, min(panel_band.bottom, center_y + y_off))
                best_lit_w = max(best_lit_w, count_non_black_row(img, sample_y, abs_x0, abs_width))
                if row.family != "lit":
                    best_family_w = max(
                        best_family_w,
                        count_family_row(img, sample_y, abs_x0, abs_width, row.family),
                    )
            if row.panel == "drop":
                best_w = best_family_w
            elif best_family_w > 0:
                best_w = best_family_w
            else:
                best_w = best_lit_w
        elif row.panel in PANEL_BY_KEY:
            best_w = 0
        else:
            best_w = 0

        # Rescale bar width back to VRAM pixel units for consistent interpretation
        if scale_x > 0 and scale_x != 1.0:
            best_w = int(round(best_w / scale_x))
        if include_zero or best_w > 0:
            rows.append(
                {
                    "key": row.key,
                    "panel": row.panel,
                    "y": row.y,
                    "width": best_w,
                    "desc": row.desc,
                    "decode_hint": row.decode_hint,
                }
            )

    interpreted = build_interpreted(rows)

    return {
        "file": path,
        "image_size": {"width": img.width, "height": img.height},
        "viewport": {
            "left": viewport_box[0],
            "top": viewport_box[1],
            "width": viewport_box[2] - viewport_box[0],
            "height": viewport_box[3] - viewport_box[1],
        },
        "panel_bands": {
            key: {
                "top": band.top,
                "bottom": band.bottom,
                "left": band.left,
                "right": band.right,
            }
            for key, band in panel_bands.items()
        },
        "scale": {"x": round(scale_x, 4), "y": round(scale_y, 4)},
        "scan": {"x0": x0, "width": scan_width},
        "rows": rows,
        "interpreted": interpreted,
    }


def _scaled_actor_row_value(
    rgb_img: Image.Image,
    sample_y: int,
    family: str,
    scale_x: float,
    panel_x: int,
    scan_width: int,
) -> int:
    best = 0
    abs_x0 = max(0, int(round(panel_x * scale_x)))
    abs_width = max(1, int(round(scan_width * scale_x)))
    for y_off in (-1, 0, 1):
        yy = max(0, min(rgb_img.height - 1, sample_y + y_off))
        best = max(best, count_family_row(rgb_img, yy, abs_x0, abs_width, family))
    if scale_x > 0 and scale_x != 1.0:
        best = int(round(best / scale_x))
    return max(0, min(ACTOR_PANEL_SCAN_W, best))


def decode_actor_panel(
    image: Image.Image | str | Path,
    baseline_image: Image.Image | str | Path | None = None,
) -> Dict[str, object]:
    if not isinstance(image, Image.Image):
        img = Image.open(image).convert("RGB")
    else:
        img = image.convert("RGB")

    source = "single-image"
    if baseline_image is not None:
        if not isinstance(baseline_image, Image.Image):
            baseline = Image.open(baseline_image).convert("RGB")
        else:
            baseline = baseline_image.convert("RGB")
        if baseline.size != img.size:
            raise ValueError("baseline image size does not match overlay image size")
        img = ImageChops.difference(img, baseline)
        source = "image-diff"

    scale_x = img.width / float(NATIVE_W)
    scale_y = img.height / float(HEADLESS_NATIVE_H)

    characters: List[Dict[str, object]] = []
    for entity_index, entity_name in enumerate(ACTOR_PANEL_ENTITIES):
        base_y = ACTOR_PANEL_Y + 2 + (entity_index * ACTOR_PANEL_ENTITY_STRIDE)
        values: Dict[str, int] = {}
        for key, row_offset in ACTOR_PANEL_ROW_OFFSETS.items():
            sample_y = int(round((base_y + row_offset) * scale_y))
            values[key] = _scaled_actor_row_value(
                img,
                sample_y,
                ACTOR_PANEL_FAMILIES[key],
                scale_x,
                ACTOR_PANEL_DATA_X,
                ACTOR_PANEL_SCAN_W,
            )

        if values["bbox_width"] <= 0 or values["bbox_height"] <= 0:
            continue

        centroid_x = (values["centroid_x"] * 639.0) / 63.0
        centroid_y = (values["centroid_y"] * 479.0) / 63.0
        bbox_width = float(values["bbox_width"])
        bbox_height = float(values["bbox_height"])
        left = int(round(centroid_x - (bbox_width / 2.0)))
        top = int(round(centroid_y - (bbox_height / 2.0)))
        right = int(round(left + bbox_width))
        bottom = int(round(top + bbox_height))

        characters.append(
            {
                "character": entity_name,
                "draw_count": 1,
                "bbox": {
                    "left": left,
                    "top": top,
                    "right": right,
                    "bottom": bottom,
                    "width": int(round(bbox_width)),
                    "height": int(round(bbox_height)),
                },
                "centroid": {
                    "x": round(centroid_x, 3),
                    "y": round(centroid_y, 3),
                },
                "sprite_sources": [],
                "overlay_metrics": values,
            }
        )

    return {
        "character_count": len(characters),
        "characters": characters,
        "source": source,
        "panel": {
            "x": ACTOR_PANEL_X,
            "y": ACTOR_PANEL_Y,
            "data_x": ACTOR_PANEL_DATA_X,
            "scan_width": ACTOR_PANEL_SCAN_W,
        },
    }


def print_human(result: Dict[str, object]) -> None:
    print(f"file: {result['file']}")
    size = result["image_size"]
    viewport = result.get("viewport", {})
    scan = result["scan"]
    sc = result.get("scale", {})
    scale_str = ""
    if sc:
        sx, sy = sc.get("x", 1.0), sc.get("y", 1.0)
        if sx != 1.0 or sy != 1.0:
            scale_str = f"  scale: {sx:.4f}x{sy:.4f}"
    viewport_str = ""
    if viewport:
        viewport_str = (
            f"  viewport: x={viewport['left']} y={viewport['top']}"
            f" {viewport['width']}x{viewport['height']}"
        )
    print(f"image: {size['width']}x{size['height']}{viewport_str}  scan: x={scan['x0']}..{scan['x0'] + scan['width'] - 1}{scale_str}")
    print("rows:")
    for row in result["rows"]:
        hint = f"  hint: {row['decode_hint']}" if row["decode_hint"] else ""
        print(f"  - {row['key']:<28} y={row['y']:<3} width={row['width']:<3} {row['desc']}{hint}")
    interpreted = result.get("interpreted", {})
    if interpreted:
        print("interpreted:")
        if "pilot_pack" in interpreted:
            pilot = interpreted["pilot_pack"]
            print(
                "  - pilot_pack"
                f" active_pack_id={pilot['active_pack_id']}"
                f" hits={pilot['hits']}"
                f" fallbacks={pilot['fallbacks']}"
                f" last_hit_entry={pilot['last_hit_entry']}"
                f" last_fallback_entry={pilot['last_fallback_entry']}"
                f" active_pack_misses={pilot['active_pack_misses']}"
                f" pack_path_used={'yes' if pilot['pack_path_used'] else 'no'}"
                f" fallback_observed={'yes' if pilot['fallback_observed'] else 'no'}"
            )
        if "memory" in interpreted:
            mem = interpreted["memory"]
            print(
                "  - memory"
                f" used_pct_estimate={mem['used_pct_estimate']}"
                f" used_bytes_estimate={mem['used_bytes_estimate']}"
                f" budget_bytes_estimate={mem['budget_bytes_estimate']}"
            )
        if "ads" in interpreted:
            ads = interpreted["ads"]
            print(
                "  - ads"
                f" scene_sig={ads['scene_sig']}"
                f" scene_slot_estimate={ads['scene_slot_estimate']}"
                f" scene_tag_estimate={ads['scene_tag_estimate']}"
                f" active_threads={ads['active_threads']}"
                f" running_threads={ads['running_threads']}"
                f" replay_count={ads['replay_count']}"
                f" last_bmp_slot={ads['last_bmp_slot']}"
                f" last_bmp_frames={ads['last_bmp_frames']}"
                f" last_bmp_status={ads['last_bmp_status']}"
            )
        if "story" in interpreted:
            story = interpreted["story"]
            print(
                "  - story"
                f" seq={story['seq']}"
                f" phase={story['phase']}"
                f" scene_tag={story['scene_tag']}"
                f" ads_sig={story['ads_sig']}"
                f" prev_sig={story['prev_sig']}"
                f" next_sig={story['next_sig']}"
            )


def main() -> int:
    ap = argparse.ArgumentParser(description="Decode PS1 left debug bars from screenshot(s).")
    ap.add_argument("images", nargs="+", help="Path(s) to screenshot image files")
    ap.add_argument("--json", action="store_true", help="Output JSON")
    ap.add_argument("--include-zero", action="store_true", help="Include rows with width 0")
    ap.add_argument("--x0", type=int, default=4, help="Scan start x in VRAM coords (default: 4)")
    ap.add_argument("--width", type=int, default=90, help="Scan width in VRAM pixels (default: 90)")
    args = ap.parse_args()

    results = [decode_file(path, args.x0, args.width, args.include_zero) for path in args.images]

    if args.json:
        print(json.dumps(results if len(results) > 1 else results[0], indent=2))
    else:
        for i, result in enumerate(results):
            if i:
                print()
            print_human(result)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
