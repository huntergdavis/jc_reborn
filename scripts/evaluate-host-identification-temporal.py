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


def slice_profile(rows: list[dict]) -> dict[str, int]:
    return {
        "frame_count": len(rows),
        "active_frame_count": sum(1 for row in rows if (row.get("actor_count") or 0) > 0),
        "state_change_count": sum(1 for row in rows if row.get("state_changed")),
    }


def redact_prefix(scene: dict, rows: list[dict], prefix_len: int) -> dict:
    query_label = f"{scene.get('scene_label')} [prefix-{prefix_len}]"
    prefix_rows = rows[:prefix_len]
    summary = dict(scene.get("scene_summary") or {})
    summary["scene_signature"] = None
    summary["identification_traits"] = [
        trait for trait in summary.get("identification_traits", []) if not trait.startswith("family:")
    ]
    summary["timeline_signature"] = " > ".join(
        f"{row['frame_number']}:{row['frame_state']}" for row in prefix_rows
    )

    redacted_rows = []
    for row in prefix_rows:
        cloned = dict(row)
        cloned["scene_label"] = query_label
        cloned["frame_signature"] = None
        cloned["identification_tokens"] = [
            token for token in cloned.get("identification_tokens", []) if not token.startswith("family:")
        ]
        redacted_rows.append(cloned)

    expected_status = (
        "identified"
        if any(row.get("frame_state") != "background_only" for row in prefix_rows)
        else "unknown"
    )

    return {
        "scene_label": query_label,
        "scene_family": scene.get("scene_family"),
        "scene_summary": summary,
        "rows": redacted_rows,
        "_expected_scene_label": scene.get("scene_label"),
        "_expected_status": expected_status,
        "_prefix_len": prefix_len,
    }


def build_queries(database: dict) -> list[dict]:
    queries = []
    for scene in database.get("scenes", []):
        rows = list(scene.get("rows", []))
        for prefix_len in range(1, len(rows) + 1):
            queries.append(redact_prefix(scene, rows, prefix_len))
    return queries


def evaluate(database: dict) -> dict:
    query_scenes = build_queries(database)
    report = identify(database, {"root": database.get("root"), "scenes": query_scenes})
    expected_by_label = {scene["scene_label"]: scene for scene in query_scenes}

    rows = []
    failures: list[str] = []
    by_expected_scene: dict[str, list[dict]] = {}

    for row in report.get("rows", []):
        query_label = row.get("query_scene_label")
        expected = expected_by_label[query_label]
        best = row.get("best_match") or {}
        second = row.get("second_match") or {}
        margin = float(row.get("score_margin") or 0.0)
        best_score = float(best.get("score") or 0.0)
        second_score = float(second.get("score") or 0.0)
        ratio = (best_score / second_score) if second_score > 0.0 else None

        record = {
            "query_scene_label": query_label,
            "expected_scene_label": expected["_expected_scene_label"],
            "expected_status": expected["_expected_status"],
            "prefix_len": expected["_prefix_len"],
            "status": row.get("identification_status"),
            "best_scene_label": best.get("scene_label"),
            "best_score": best.get("score"),
            "score_margin": row.get("score_margin"),
            "best_to_second_ratio": ratio,
        }
        record.update(
            {
                "query_frame_count": slice_profile(expected.get("rows", []))["frame_count"],
                "query_active_frame_count": slice_profile(expected.get("rows", []))["active_frame_count"],
                "query_state_change_count": slice_profile(expected.get("rows", []))["state_change_count"],
            }
        )
        rows.append(record)
        by_expected_scene.setdefault(expected["_expected_scene_label"], []).append(record)

        if record["status"] != record["expected_status"]:
            failures.append(
                f"{query_label}: expected status={record['expected_status']}, got {record['status']}"
            )
        if record["expected_status"] == "identified" and record["best_scene_label"] != record["expected_scene_label"]:
            failures.append(f"{query_label}: best_match={record['best_scene_label']}")
        if record["expected_status"] == "identified" and record["query_active_frame_count"] < 1:
            failures.append(f"{query_label}: identified prefix lacks active semantic evidence")

    transition_counts = {}
    min_identified_margin = None
    min_identified_ratio = None
    max_score_drop = 0.0
    max_identified_margin_drop = 0.0

    for scene_label, scene_rows in by_expected_scene.items():
        scene_rows.sort(key=lambda row: row["prefix_len"])
        previous_status = None
        previous_score = None
        previous_identified_margin = None
        seen_identified = False
        transition_count = 0
        for record in scene_rows:
            status = record["status"]
            score = float(record["best_score"] or 0.0)
            if previous_status is not None and status != previous_status:
                transition_count += 1
            if previous_score is not None and score < previous_score:
                score_drop = previous_score - score
                if score_drop > max_score_drop:
                    max_score_drop = score_drop
                failures.append(
                    f"{scene_label}: best score regressed at prefix {record['prefix_len']} by {score_drop:.6f}"
                )
            if status == "identified":
                seen_identified = True
                margin = float(record["score_margin"] or 0.0)
                ratio = record["best_to_second_ratio"]
                if min_identified_margin is None or margin < min_identified_margin:
                    min_identified_margin = margin
                if ratio is not None and (min_identified_ratio is None or ratio < min_identified_ratio):
                    min_identified_ratio = ratio
                if previous_identified_margin is not None and margin < previous_identified_margin:
                    margin_drop = previous_identified_margin - margin
                    if margin_drop > max_identified_margin_drop:
                        max_identified_margin_drop = margin_drop
                    failures.append(
                        f"{scene_label}: identified margin regressed at prefix {record['prefix_len']} by {margin_drop:.6f}"
                    )
                previous_identified_margin = margin
            elif seen_identified:
                failures.append(f"{scene_label}: status regressed from identified to {status}")
            previous_status = status
            previous_score = score

        transition_counts[scene_label] = transition_count
        if transition_count > 1:
            failures.append(f"{scene_label}: too many status transitions ({transition_count})")

    return {
        "rows": rows,
        "query_count": len(rows),
        "transition_counts": transition_counts,
        "min_identified_margin": min_identified_margin,
        "min_identified_ratio": min_identified_ratio,
        "max_score_drop": max_score_drop,
        "max_identified_margin_drop": max_identified_margin_drop,
        "passed": not failures,
        "failures": failures,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Evaluate temporal stability of scene identification")
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
