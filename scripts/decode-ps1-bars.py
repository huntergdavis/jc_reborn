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
from typing import Callable, Dict, List

try:
    from PIL import Image
except Exception:
    print("error: Pillow is required (pip install pillow)", file=sys.stderr)
    raise


@dataclass(frozen=True)
class RowDef:
    key: str
    y: int
    panel: str
    desc: str
    decode_hint: str = ""


ROWS: List[RowDef] = [
    RowDef("drop_thread_drops", 4, "drop", "gStatThreadDrops"),
    RowDef("drop_replay_drops", 7, "drop", "gStatReplayDrops"),
    RowDef("drop_bmp_frame_cap", 10, "drop", "gStatBmpFrameCapHits"),
    RowDef("drop_bmp_short_load", 13, "drop", "gStatBmpShortLoads"),

    RowDef("ads_active_threads", 91, "ads", "ps1AdsDbgActiveThreads", "value ~= width"),
    RowDef("ads_mini", 94, "ads", "ps1AdsDbgMini", "value ~= width"),
    RowDef("ads_stalled_frames_div4", 97, "ads", "ps1AdsDbgStalledFrames >> 2", "stalled_frames ~= width * 4"),
    RowDef("ads_progress_pulse", 100, "ads", "ps1AdsDbgProgressPulse", "value ~= width"),
    RowDef("ads_scene_sig", 103, "ads", "((sceneSlot & 0x7) << 3) | (sceneTag & 0x7)", "value ~= width"),
    RowDef("ads_replay_count", 106, "ads", "ps1AdsDbgReplayCount", "value ~= width"),
    RowDef("ads_running_threads", 109, "ads", "ps1AdsDbgRunningThreads", "value ~= width"),
    RowDef("ads_terminated_threads", 112, "ads", "ps1AdsDbgTerminatedThreads", "value ~= width"),
    RowDef("ads_thread_timer", 115, "ads", "ps1AdsDbgThreadTimer", "value ~= width"),
    RowDef("ads_thread_delay", 118, "ads", "ps1AdsDbgThreadDelay", "value ~= width"),
    RowDef("ads_update_delay", 121, "ads", "grUpdateDelay", "value ~= width"),
    RowDef("ads_replay_try_frame", 124, "ads", "ps1AdsDbgReplayTryFrame", "value ~= width"),
    RowDef("ads_replay_draw_frame", 127, "ads", "ps1AdsDbgReplayDrawFrame", "value ~= width"),
    RowDef("ads_replay_reject_epoch", 130, "ads", "ps1AdsDbgReplayRejectEpoch", "value ~= width"),
    RowDef("ads_replay_reject_gen", 133, "ads", "ps1AdsDbgReplayRejectGen", "value ~= width"),
    RowDef("ads_replay_reject_slot", 136, "ads", "ps1AdsDbgReplayRejectSlot", "value ~= width"),
    RowDef("ads_replay_reject_sprite", 139, "ads", "ps1AdsDbgReplayRejectSprite", "value ~= width"),
    RowDef("ads_replay_flip_frame", 142, "ads", "ps1AdsDbgReplayFlipFrame", "value ~= width"),
    RowDef("ads_scene_seq", 145, "ads", "ps1AdsDbgSceneSeq", "value ~= width"),
    RowDef("ads_scene_frames", 148, "ads", "ps1AdsDbgSceneFrames", "value ~= width"),
    RowDef("ads_scene_try", 151, "ads", "ps1AdsDbgSceneTry", "value ~= width"),
    RowDef("ads_scene_draw", 154, "ads", "ps1AdsDbgSceneDraw", "value ~= width"),
    RowDef("ads_scene_reject_epoch", 157, "ads", "ps1AdsDbgSceneRejectEpoch", "value ~= width"),
    RowDef("ads_scene_reject_gen", 160, "ads", "ps1AdsDbgSceneRejectGen", "value ~= width"),
    RowDef("ads_scene_reject_slot", 163, "ads", "ps1AdsDbgSceneRejectSlot", "value ~= width"),
    RowDef("ads_scene_reject_sprite", 166, "ads", "ps1AdsDbgSceneRejectSprite", "value ~= width"),
    RowDef("ads_scene_stall_max_div4", 169, "ads", "ps1AdsDbgSceneStallMax >> 2", "stall_max ~= width * 4"),
    RowDef("ads_no_draw_streak_div2", 172, "ads", "ps1AdsDbgNoDrawStreak >> 1", "no_draw_streak ~= width * 2"),

    RowDef("mem_used_pct_scaled", 175, "mem", "(used/budget)*63", "used_pct ~= width * 100 / 63"),
    RowDef("mem_loaded_bmp", 178, "mem", "gStatLoadedBmp", "value ~= width"),
    RowDef("mem_loaded_ttm", 181, "mem", "gStatLoadedTtm", "value ~= width"),
    RowDef("mem_loaded_ads", 184, "mem", "gStatLoadedAds", "value ~= width"),
    RowDef("mem_used_16k", 187, "mem", "used_bytes >> 14", "used_bytes ~= width * 16384"),
    RowDef("mem_budget_16k", 190, "mem", "budget_bytes >> 14", "budget_bytes ~= width * 16384"),

    RowDef("story_seq", 223, "story", "ps1StoryDbgSeq", "value ~= width"),
    RowDef("story_phase", 226, "story", "ps1StoryDbgPhase", "value ~= width"),
    RowDef("story_scene_tag", 229, "story", "ps1StoryDbgSceneTag", "value ~= width"),
    RowDef("story_prev_sig", 232, "story", "(prevSpot*8)+prevHdg", "value ~= width"),
    RowDef("story_next_sig", 235, "story", "(nextSpot*8)+nextHdg", "value ~= width"),
]


def count_non_black_row(rgb_img: Image.Image, y: int, x0: int, width: int) -> int:
    px = rgb_img.load()
    x1 = min(rgb_img.width, x0 + width)
    yy = max(0, min(rgb_img.height - 1, y))
    count = 0
    for x in range(x0, x1):
        if px[x, yy] != (0, 0, 0):
            count += 1
    return count


def decode_file(path: str, x0: int, scan_width: int, include_zero: bool) -> Dict[str, object]:
    img = Image.open(path).convert("RGB")
    rows = []
    for row in ROWS:
        w = count_non_black_row(img, row.y, x0, scan_width)
        if include_zero or w > 0:
            rows.append(
                {
                    "key": row.key,
                    "panel": row.panel,
                    "y": row.y,
                    "width": w,
                    "desc": row.desc,
                    "decode_hint": row.decode_hint,
                }
            )

    return {
        "file": path,
        "image_size": {"width": img.width, "height": img.height},
        "scan": {"x0": x0, "width": scan_width},
        "rows": rows,
    }


def print_human(result: Dict[str, object]) -> None:
    print(f"file: {result['file']}")
    size = result["image_size"]
    scan = result["scan"]
    print(f"image: {size['width']}x{size['height']}  scan: x={scan['x0']}..{scan['x0'] + scan['width'] - 1}")
    print("rows:")
    for row in result["rows"]:
        hint = f"  hint: {row['decode_hint']}" if row["decode_hint"] else ""
        print(f"  - {row['key']:<28} y={row['y']:<3} width={row['width']:<3} {row['desc']}{hint}")


def main() -> int:
    ap = argparse.ArgumentParser(description="Decode PS1 left debug bars from screenshot(s).")
    ap.add_argument("images", nargs="+", help="Path(s) to screenshot image files")
    ap.add_argument("--json", action="store_true", help="Output JSON")
    ap.add_argument("--include-zero", action="store_true", help="Include rows with width 0")
    ap.add_argument("--x0", type=int, default=4, help="Scan start x (default: 4)")
    ap.add_argument("--width", type=int, default=90, help="Scan width in pixels (default: 90)")
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
