#!/usr/bin/env python3
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path


def load_identify():
    script_path = Path(__file__).with_name("identify-host-scene.py")
    spec = importlib.util.spec_from_file_location("identify_host_scene", script_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load identifier from {script_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.identify


identify = load_identify()


def redact_rows(rows: list[dict], query_label: str) -> list[dict]:
    output = []
    for row in rows:
        cloned = dict(row)
        cloned["scene_label"] = query_label
        cloned["frame_signature"] = None
        cloned["identification_tokens"] = [
            token for token in cloned.get("identification_tokens", []) if not token.startswith("family:")
        ]
        output.append(cloned)
    return output


def build_queries(database: dict) -> list[dict]:
    scenes = {scene["scene_label"]: scene for scene in database.get("scenes", [])}
    queries: list[dict] = []

    for label, scene in scenes.items():
        bg_rows = [row for row in scene.get("rows", []) if row.get("frame_state") == "background_only"]
        if bg_rows:
            queries.append(
                {
                    "scene_label": f"{label} [background-only]",
                    "scene_family": "unknown",
                    "scene_summary": {
                        "scene_signature": None,
                        "identification_traits": [],
                    },
                    "rows": redact_rows(bg_rows, f"{label} [background-only]"),
                    "_expected_status": "non_identified",
                }
            )

    fishing = scenes.get("FISHING 1")
    mary = scenes.get("MARY 1")
    if fishing and mary:
        fishing_active = [row for row in fishing.get("rows", []) if row.get("frame_state") != "background_only"]
        mary_active = [row for row in mary.get("rows", []) if row.get("frame_state") != "background_only"]
        if fishing_active and mary_active:
            mixed_rows = [fishing_active[-1], mary_active[0]]
            queries.append(
                {
                    "scene_label": "MIXED [active-pair]",
                    "scene_family": "unknown",
                    "scene_summary": {
                        "scene_signature": None,
                        "identification_traits": [],
                    },
                    "rows": redact_rows(mixed_rows, "MIXED [active-pair]"),
                    "_expected_status": "non_identified",
                }
            )
            queries.append(
                {
                    "scene_label": "MIXED [fishing-bg-plus-mary-active]",
                    "scene_family": "unknown",
                    "scene_summary": {
                        "scene_signature": None,
                        "identification_traits": [],
                    },
                    "rows": redact_rows(
                        [fishing.get("rows", [])[0], fishing.get("rows", [])[1], mary_active[0]],
                        "MIXED [fishing-bg-plus-mary-active]",
                    ),
                    "_expected_status": "non_identified",
                }
            )
            queries.append(
                {
                    "scene_label": "MIXED [mary-bg-plus-fishing-active]",
                    "scene_family": "unknown",
                    "scene_summary": {
                        "scene_signature": None,
                        "identification_traits": [],
                    },
                    "rows": redact_rows(
                        [mary.get("rows", [])[0], mary.get("rows", [])[1], fishing_active[-1]],
                        "MIXED [mary-bg-plus-fishing-active]",
                    ),
                    "_expected_status": "non_identified",
                }
            )
            queries.append(
                {
                    "scene_label": "MIXED [interleaved-contradiction]",
                    "scene_family": "unknown",
                    "scene_summary": {
                        "scene_signature": None,
                        "identification_traits": [],
                    },
                    "rows": redact_rows(
                        [fishing.get("rows", [])[0], mary.get("rows", [])[1], fishing_active[-1], mary_active[0]],
                        "MIXED [interleaved-contradiction]",
                    ),
                    "_expected_status": "non_identified",
                }
            )

    return queries


def evaluate(database: dict) -> dict:
    query_scenes = build_queries(database)
    report = identify(database, {"root": database.get("root"), "scenes": query_scenes})

    rows = []
    failures: list[str] = []
    max_best_score = None
    max_margin = None
    ambiguous_count = 0
    unknown_count = 0

    expectations = {scene["scene_label"]: scene.get("_expected_status", "non_identified") for scene in query_scenes}

    for row in report.get("rows", []):
        query_label = row.get("query_scene_label")
        expected_status = expectations.get(query_label, "unknown")
        status = row.get("identification_status")
        best = row.get("best_match") or {}
        margin = float(row.get("score_margin") or 0.0)
        best_score = float(best.get("score") or 0.0)

        if max_best_score is None or best_score > max_best_score:
            max_best_score = best_score
        if max_margin is None or margin > max_margin:
            max_margin = margin

        if expected_status == "non_identified":
            if status == "identified":
                failures.append(f"{query_label}: challenge query must not identify as {best.get('scene_label')}")
            elif status == "ambiguous":
                ambiguous_count += 1
            elif status == "unknown":
                unknown_count += 1
            else:
                failures.append(f"{query_label}: unexpected status {status}")
        elif status != expected_status:
            failures.append(f"{query_label}: expected {expected_status}, got {status}")

        rows.append(
            {
                "query_scene_label": query_label,
                "expected_status": expected_status,
                "status": status,
                "best_scene_label": best.get("scene_label"),
                "best_score": best.get("score"),
                "score_margin": row.get("score_margin"),
                "reason": row.get("identification_reason"),
            }
        )

    return {
        "rows": rows,
        "query_count": len(rows),
        "max_best_score": max_best_score,
        "max_margin": max_margin,
        "ambiguous_count": ambiguous_count,
        "unknown_count": unknown_count,
        "passed": not failures,
        "failures": failures,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Evaluate identification rejection on challenge queries")
    parser.add_argument("--semantic-json", type=Path, required=True)
    parser.add_argument("--out-json", type=Path)
    args = parser.parse_args()

    semantic = json.loads(args.semantic_json.read_text(encoding="utf-8"))
    payload = json.dumps(evaluate(semantic), indent=2) + "\n"
    if args.out_json:
        args.out_json.write_text(payload, encoding="utf-8")
    else:
        print(payload, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
