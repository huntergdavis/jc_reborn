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

UNKNOWN_SCORE_LIMIT = 110.0
UNKNOWN_MARGIN_LIMIT = 35.0
AMBIGUOUS_SCORE_LIMIT = 150.0
AMBIGUOUS_MARGIN_LIMIT = 150.0


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


def first_row_with_pose(rows: list[dict], pose_label: str) -> dict | None:
    for row in rows:
        if pose_label in (row.get("pose_labels") or []):
            return row
    return None


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
            fishing_walk = first_row_with_pose(fishing_active, "johnny_walking_pose") or fishing_active[-1]
            mary_walk = first_row_with_pose(mary_active, "johnny_walking_pose") or mary_active[0]
            mary_fish = first_row_with_pose(mary_active, "johnny_fishing_pose") or mary_active[-1]
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
                    "scene_label": "MIXED [pose-conflict-cross-scene]",
                    "scene_family": "unknown",
                    "scene_summary": {
                        "scene_signature": None,
                        "identification_traits": [],
                    },
                    "rows": redact_rows(
                        [fishing_walk, mary_fish],
                        "MIXED [pose-conflict-cross-scene]",
                    ),
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
    max_unknown_best_score = None
    max_unknown_margin = None
    max_ambiguous_best_score = None
    max_ambiguous_margin = None
    tightest: dict | None = None

    expectations = {scene["scene_label"]: scene.get("_expected_status", "non_identified") for scene in query_scenes}

    for row in report.get("rows", []):
        query_label = row.get("query_scene_label")
        expected_status = expectations.get(query_label, "unknown")
        status = row.get("identification_status")
        best = row.get("best_match") or {}
        margin = float(row.get("score_margin") or 0.0)
        best_score = float(best.get("score") or 0.0)
        score_limit = None
        margin_limit = None
        score_headroom = None
        margin_headroom = None

        if max_best_score is None or best_score > max_best_score:
            max_best_score = best_score
        if max_margin is None or margin > max_margin:
            max_margin = margin

        if expected_status == "non_identified":
            if status == "identified":
                failures.append(f"{query_label}: challenge query must not identify as {best.get('scene_label')}")
            elif status == "ambiguous":
                ambiguous_count += 1
                score_limit = AMBIGUOUS_SCORE_LIMIT
                margin_limit = AMBIGUOUS_MARGIN_LIMIT
                score_headroom = round(score_limit - best_score, 6)
                margin_headroom = round(margin_limit - margin, 6)
                if max_ambiguous_best_score is None or best_score > max_ambiguous_best_score:
                    max_ambiguous_best_score = best_score
                if max_ambiguous_margin is None or margin > max_ambiguous_margin:
                    max_ambiguous_margin = margin
                if best_score > score_limit:
                    failures.append(f"{query_label}: ambiguous challenge score too high ({best_score:.6f})")
                if margin > margin_limit:
                    failures.append(f"{query_label}: ambiguous challenge margin too high ({margin:.6f})")
            elif status == "unknown":
                unknown_count += 1
                score_limit = UNKNOWN_SCORE_LIMIT
                margin_limit = UNKNOWN_MARGIN_LIMIT
                score_headroom = round(score_limit - best_score, 6)
                margin_headroom = round(margin_limit - margin, 6)
                if max_unknown_best_score is None or best_score > max_unknown_best_score:
                    max_unknown_best_score = best_score
                if max_unknown_margin is None or margin > max_unknown_margin:
                    max_unknown_margin = margin
                if best_score > score_limit:
                    failures.append(f"{query_label}: unknown challenge score too high ({best_score:.6f})")
                if margin > margin_limit:
                    failures.append(f"{query_label}: unknown challenge margin too high ({margin:.6f})")
            else:
                failures.append(f"{query_label}: unexpected status {status}")
        elif status != expected_status:
            failures.append(f"{query_label}: expected {expected_status}, got {status}")

        if score_limit is not None and margin_limit is not None:
            candidates = [
                {
                    "metric": "score",
                    "value": round(best_score, 6),
                    "limit": score_limit,
                    "headroom": score_headroom,
                    "pressure": 0.0 if score_limit <= 0 else round(best_score / score_limit, 6),
                },
                {
                    "metric": "margin",
                    "value": round(margin, 6),
                    "limit": margin_limit,
                    "headroom": margin_headroom,
                    "pressure": 0.0 if margin_limit <= 0 else round(margin / margin_limit, 6),
                },
            ]
            row_tightest = min(candidates, key=lambda item: (item["headroom"], -item["pressure"], item["metric"]))
            if tightest is None or (
                row_tightest["headroom"],
                -row_tightest["pressure"],
                row_tightest["metric"],
                query_label,
            ) < (
                tightest["headroom"],
                -tightest["pressure"],
                tightest["metric"],
                tightest["query_scene_label"],
            ):
                tightest = {
                    "query_scene_label": query_label,
                    "status": status,
                    "best_scene_label": best.get("scene_label"),
                    "metric": row_tightest["metric"],
                    "value": row_tightest["value"],
                    "limit": row_tightest["limit"],
                    "headroom": row_tightest["headroom"],
                    "pressure": row_tightest["pressure"],
                }

        rows.append(
            {
                "query_scene_label": query_label,
                "expected_status": expected_status,
                "status": status,
                "best_scene_label": best.get("scene_label"),
                "best_score": best.get("score"),
                "borrowed_background_risk": (row.get("decision_context") or {}).get("best_borrowed_background_risk"),
                "borrowed_background_mismatch": (row.get("decision_context") or {}).get("best_borrowed_background_mismatch"),
                "score_margin": row.get("score_margin"),
                "score_limit": score_limit,
                "score_headroom": score_headroom,
                "margin_limit": margin_limit,
                "margin_headroom": margin_headroom,
                "reason": row.get("identification_reason"),
            }
        )

    return {
        "rows": rows,
        "query_count": len(rows),
        "unknown_score_limit": UNKNOWN_SCORE_LIMIT,
        "unknown_margin_limit": UNKNOWN_MARGIN_LIMIT,
        "ambiguous_score_limit": AMBIGUOUS_SCORE_LIMIT,
        "ambiguous_margin_limit": AMBIGUOUS_MARGIN_LIMIT,
        "max_best_score": max_best_score,
        "max_margin": max_margin,
        "max_unknown_best_score": max_unknown_best_score,
        "max_unknown_margin": max_unknown_margin,
        "max_ambiguous_best_score": max_ambiguous_best_score,
        "max_ambiguous_margin": max_ambiguous_margin,
        "unknown_score_headroom": None if max_unknown_best_score is None else round(UNKNOWN_SCORE_LIMIT - max_unknown_best_score, 6),
        "unknown_margin_headroom": None if max_unknown_margin is None else round(UNKNOWN_MARGIN_LIMIT - max_unknown_margin, 6),
        "ambiguous_score_headroom": None if max_ambiguous_best_score is None else round(AMBIGUOUS_SCORE_LIMIT - max_ambiguous_best_score, 6),
        "ambiguous_margin_headroom": None if max_ambiguous_margin is None else round(AMBIGUOUS_MARGIN_LIMIT - max_ambiguous_margin, 6),
        "ambiguous_count": ambiguous_count,
        "unknown_count": unknown_count,
        "tightest_query_label": None if tightest is None else tightest["query_scene_label"],
        "tightest_status": None if tightest is None else tightest["status"],
        "tightest_best_scene_label": None if tightest is None else tightest["best_scene_label"],
        "tightest_metric": None if tightest is None else tightest["metric"],
        "tightest_value": None if tightest is None else tightest["value"],
        "tightest_limit": None if tightest is None else tightest["limit"],
        "tightest_headroom": None if tightest is None else tightest["headroom"],
        "tightest_pressure": None if tightest is None else tightest["pressure"],
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
