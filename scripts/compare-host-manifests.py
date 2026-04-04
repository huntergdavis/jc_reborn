#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path


def load_manifest(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def index_manifest(manifest: dict) -> dict[tuple[str, int], dict]:
    out = {}
    for scene in manifest.get("scenes", []):
        label = scene["scene_label"]
        for row in scene.get("rows", []):
            out[(label, int(row["frame_number"]))] = row
    return out


def compare(base: dict, other: dict) -> dict:
    base_idx = index_manifest(base)
    other_idx = index_manifest(other)
    keys = sorted(set(base_idx) | set(other_idx))
    rows = []
    for key in keys:
        base_row = base_idx.get(key)
        other_row = other_idx.get(key)
        problems = []
        if base_row is None:
            problems.append("missing_in_base")
        if other_row is None:
            problems.append("missing_in_other")
        if base_row and other_row:
            if base_row.get("actor_summary", {}) != other_row.get("actor_summary", {}):
                problems.append("actor_summary_mismatch")
            if base_row.get("bmp_names", []) != other_row.get("bmp_names", []):
                problems.append("bmp_names_mismatch")
            if int(base_row.get("actor_candidate_draw_count", 0)) != int(other_row.get("actor_candidate_draw_count", 0)):
                problems.append("actor_candidate_count_mismatch")
            if int(base_row.get("visible_unique_draw_count", 0)) != int(other_row.get("visible_unique_draw_count", 0)):
                problems.append("visible_unique_draw_count_mismatch")
        rows.append(
            {
                "scene_label": key[0],
                "frame_number": key[1],
                "status": "ok" if not problems else "mismatch",
                "problems": problems,
                "base": base_row,
                "other": other_row,
            }
        )
    mismatches = [row for row in rows if row["status"] == "mismatch"]
    return {"rows": rows, "mismatch_count": len(mismatches), "mismatches": mismatches}


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare two host script manifests for reproducibility")
    parser.add_argument("--base-json", type=Path, required=True)
    parser.add_argument("--other-json", type=Path, required=True)
    parser.add_argument("--out-json", type=Path)
    args = parser.parse_args()

    report = compare(load_manifest(args.base_json), load_manifest(args.other_json))
    payload = json.dumps(report, indent=2) + "\n"
    if args.out_json:
        args.out_json.write_text(payload, encoding="utf-8")
    else:
        print(payload, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
