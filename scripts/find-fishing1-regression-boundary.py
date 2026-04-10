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


def compact(row):
    cov = row.get("coverage") or {}
    result = row.get("result") or {}
    return {
        "result_json": row.get("result_json"),
        "state_hash": result.get("state_hash"),
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
