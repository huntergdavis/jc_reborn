#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path


def load_expectations(path: Path) -> dict[str, dict]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    return {scene["scene_label"]: scene for scene in payload.get("scenes", [])}


def compare(manifest: dict, expectations: dict[str, dict]) -> dict:
    rows = []
    for scene in manifest.get("scenes", []):
        scene_label = scene["scene_label"]
        expected_scene = expectations.get(scene_label)
        by_frame = {
            int(item["frame_number"]): item
            for item in (expected_scene or {}).get("frame_expectations", [])
        }
        preferred_prefixes = tuple(
            (expected_scene or {}).get("resource_expectations", {}).get("preferred_actor_bmp_prefixes", [])
        )
        for row in scene.get("rows", []):
            frame_no = int(row["frame_number"])
            expected = by_frame.get(frame_no)
            actual_entities = sorted(row.get("actor_summary", {}).keys())
            actual_bmps = row.get("bmp_names", [])
            problems = []
            if expected is None:
                status = "no_expectation"
            else:
                want_any = bool(expected.get("expect_any_actor"))
                expect_entities = expected.get("expect_entities", [])
                required_prefixes = tuple(expected.get("required_actor_bmp_prefixes", []))
                required_bmps = list(expected.get("required_actor_bmps", []))
                bmp_prefix_match = any(
                    bmp.upper().startswith(prefix.upper())
                    for bmp in actual_bmps
                    for prefix in preferred_prefixes
                )
                required_prefix_match = any(
                    bmp.upper().startswith(prefix.upper())
                    for bmp in actual_bmps
                    for prefix in required_prefixes
                )
                if want_any and not actual_entities:
                    problems.append("missing_actor")
                if not want_any and actual_entities:
                    problems.append("unexpected_actor")
                if expect_entities:
                    if not any(entity in actual_entities for entity in expect_entities):
                        problems.append("expected_entity_missing")
                if preferred_prefixes and actual_bmps and not bmp_prefix_match:
                    problems.append("expected_bmp_family_missing")
                if required_prefixes and not required_prefix_match:
                    problems.append("required_bmp_family_missing")
                if required_bmps and actual_bmps != required_bmps:
                    problems.append("required_actor_bmps_mismatch")
                if required_bmps and not actual_bmps:
                    problems.append("required_actor_bmps_missing")
                status = "ok" if not problems else "mismatch"
            rows.append(
                {
                    "scene_label": scene_label,
                    "frame_number": frame_no,
                    "frame_name": row["frame_name"],
                    "image_path": row.get("image_path"),
                    "actual_entities": actual_entities,
                    "actual_bmps": actual_bmps,
                    "expected": expected,
                    "status": status,
                    "problems": problems,
                }
            )
    mismatches = [row for row in rows if row["status"] == "mismatch"]
    return {
        "rows": rows,
        "mismatch_count": len(mismatches),
        "mismatches": mismatches,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare host script manifest against scene expectations")
    parser.add_argument("--manifest-json", type=Path, required=True)
    parser.add_argument("--expectations-json", type=Path, required=True)
    parser.add_argument("--out-json", type=Path)
    args = parser.parse_args()

    manifest = json.loads(args.manifest_json.read_text(encoding="utf-8"))
    expectations = load_expectations(args.expectations_json)
    result = compare(manifest, expectations)
    payload = json.dumps(result, indent=2) + "\n"
    if args.out_json:
        args.out_json.write_text(payload, encoding="utf-8")
    else:
        print(payload, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
