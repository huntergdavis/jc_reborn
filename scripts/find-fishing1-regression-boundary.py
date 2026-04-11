#!/usr/bin/env python3
import argparse
import json
import re
from pathlib import Path


DIR_RE = re.compile(r"_(?P<date>\d{8})_(?P<time>\d{6})_(?P<hash>[0-9a-f]{8})$")


def parse_args():
    parser = argparse.ArgumentParser(
        description="Find the last healthy and first degraded FISHING 1 build in a scan summary."
    )
    parser.add_argument("--summary-json", required=True, help="Path to fishing1-binary-regression-summary.json")
    parser.add_argument(
        "--startup-regime",
        help=(
            "Optional startup regime to search for, e.g. "
            "'top_only_then_full_height_startup'. When set, report the last "
            "build before that regime and the first build in that regime."
        ),
    )
    parser.add_argument(
        "--min-first-visible",
        type=int,
        help=(
            "Optional first-visible threshold. When set, report the last build "
            "below this value and the first build at or above it."
        ),
    )
    parser.add_argument(
        "--min-first-full-height",
        type=int,
        help=(
            "Optional first-full-height threshold. When set, report the last "
            "build below this value and the first build at or above it."
        ),
    )
    parser.add_argument("--min-correct", type=int, default=11, help="Minimum unique correct hashes for healthy")
    parser.add_argument("--min-shoe", type=int, default=2, help="Minimum unique shoe hashes for healthy")
    parser.add_argument("--min-midgap", type=int, default=5, help="Minimum unique post-correct-pre-shoe hashes for healthy")
    parser.add_argument("--min-ocean", type=int, default=20, help="Minimum unique ocean-only hashes for healthy")
    parser.add_argument("--min-island", type=int, default=20, help="Minimum unique island-only hashes for healthy")
    return parser.parse_args()


def extract_sort_key(result_json: str):
    path = Path(result_json)
    for part in path.parts:
        match = DIR_RE.search(part)
        if match:
            return (
                match.group("date"),
                match.group("time"),
                match.group("hash"),
                part,
            )
    return ("", "", "", path.as_posix())


def is_healthy(row, args):
    cov = row.get("coverage") or {}
    return (
        cov.get("unique_correct_hashes", 0) >= args.min_correct
        and cov.get("unique_shoe_hashes", 0) >= args.min_shoe
        and cov.get("unique_post_correct_pre_shoe_hashes", 0) >= args.min_midgap
        and cov.get("unique_ocean_only_hashes", 0) >= args.min_ocean
        and cov.get("unique_island_only_hashes", 0) >= args.min_island
    )


def infer_startup_regime(row):
    startup_regime = row.get("startup_regime")
    if startup_regime:
        return startup_regime

    cov = row.get("coverage") or {}
    first_visible = cov.get("first_visible_frame")
    first_lower_half = cov.get("first_lower_half_visible_frame")
    first_full_height = cov.get("first_full_height_visible_frame")

    if first_visible is None:
        return "never_visible"
    if first_full_height == first_visible:
        return "full_height_visible_immediately"
    if first_lower_half == first_visible:
        return "boxed_or_partial_full_scene_startup"
    return "top_only_then_full_height_startup"


def reaches_visibility_target(row, args):
    cov = row.get("coverage") or {}
    first_visible = cov.get("first_visible_frame")
    first_full_height = cov.get("first_full_height_visible_frame")

    if args.min_first_visible is not None:
        if first_visible is None or first_visible < args.min_first_visible:
            return False
    if args.min_first_full_height is not None:
        if first_full_height is None or first_full_height < args.min_first_full_height:
            return False
    return args.min_first_visible is not None or args.min_first_full_height is not None


def compact(row):
    cov = row.get("coverage") or {}
    result = row.get("result") or {}
    result_ref = row.get("result_json") or row.get("result_path")
    return {
        "result_json": result_ref,
        "state_hash": result.get("state_hash"),
        "startup_regime": infer_startup_regime(row),
        "first_visible": cov.get("first_visible_frame"),
        "first_lower_half_visible": cov.get("first_lower_half_visible_frame"),
        "first_full_height_visible": cov.get("first_full_height_visible_frame"),
        "last_partial_height_visible": cov.get("last_partial_height_visible_frame"),
        "last_black": cov.get("last_black_frame"),
        "black": cov.get("unique_black_hashes", 0),
        "ocean": cov.get("unique_ocean_only_hashes", 0),
        "island": cov.get("unique_island_only_hashes", 0),
        "correct": cov.get("unique_correct_hashes", 0),
        "shoe": cov.get("unique_shoe_hashes", 0),
        "midgap": cov.get("unique_post_correct_pre_shoe_hashes", 0),
    }


def main():
    args = parse_args()
    payload = json.loads(Path(args.summary_json).read_text(encoding="utf-8"))
    rows = list(payload.get("rows") or [])
    rows.sort(key=lambda row: extract_sort_key(row.get("result_json", "")))

    if args.startup_regime:
        last_before_target = None
        first_target = None
        for row in rows:
            if infer_startup_regime(row) == args.startup_regime:
                first_target = row
                break
            last_before_target = row

        report = {
            "summary_json": str(Path(args.summary_json).resolve()),
            "startup_regime": args.startup_regime,
            "last_before_target": compact(last_before_target) if last_before_target else None,
            "first_target": compact(first_target) if first_target else None,
        }
        print(json.dumps(report, indent=2))
        return

    if args.min_first_visible is not None or args.min_first_full_height is not None:
        last_before_target = None
        first_target = None
        for row in rows:
            if reaches_visibility_target(row, args):
                first_target = row
                break
            last_before_target = row

        report = {
            "summary_json": str(Path(args.summary_json).resolve()),
            "visibility_thresholds": {
                "min_first_visible": args.min_first_visible,
                "min_first_full_height": args.min_first_full_height,
            },
            "last_before_target": compact(last_before_target) if last_before_target else None,
            "first_target": compact(first_target) if first_target else None,
        }
        print(json.dumps(report, indent=2))
        return

    last_healthy = None
    first_degraded = None
    for row in rows:
        if is_healthy(row, args):
            last_healthy = row
        elif first_degraded is None:
            first_degraded = row
            break

    report = {
        "summary_json": str(Path(args.summary_json).resolve()),
        "thresholds": {
            "min_correct": args.min_correct,
            "min_shoe": args.min_shoe,
            "min_midgap": args.min_midgap,
            "min_ocean": args.min_ocean,
            "min_island": args.min_island,
        },
        "last_healthy": compact(last_healthy) if last_healthy else None,
        "first_degraded": compact(first_degraded) if first_degraded else None,
    }
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
