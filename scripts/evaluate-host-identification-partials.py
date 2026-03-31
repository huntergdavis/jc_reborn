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


def strip_query_scene(scene: dict, rows: list[dict], variant_name: str) -> dict:
    query_label = f"{scene.get('scene_label')} [{variant_name}]"
    summary = dict(scene.get("scene_summary") or {})
    summary["scene_signature"] = None
    traits = [trait for trait in summary.get("identification_traits", []) if not trait.startswith("family:")]
    summary["identification_traits"] = traits
    summary["query_variant"] = variant_name
    summary["timeline_signature"] = " > ".join(
        f"{row['frame_number']}:{row['frame_state']}" for row in rows
    )

    query_rows = []
    for row in rows:
        cloned = dict(row)
        cloned["scene_label"] = query_label
        cloned["frame_signature"] = None
        cloned["identification_tokens"] = [
            token for token in cloned.get("identification_tokens", []) if not token.startswith("family:")
        ]
        query_rows.append(cloned)

    return {
        "scene_dir": scene.get("scene_dir"),
        "scene_label": query_label,
        "scene_family": scene.get("scene_family"),
        "scene_summary": summary,
        "rows": query_rows,
        "_expected_scene_label": scene.get("scene_label"),
        "_query_variant": variant_name,
    }


def build_variants(scene: dict) -> list[dict]:
    rows = list(scene.get("rows", []))
    if not rows:
        return []

    non_background = [row for row in rows if row.get("frame_state") != "background_only"]
    changed = [row for row in rows if row.get("state_changed")]
    tail_count = max(2, len(rows) // 2)
    variants: list[tuple[str, list[dict]]] = [
        ("full-redacted", rows),
        ("tail-half", rows[-tail_count:]),
    ]
    if non_background:
        variants.append(("active-only", non_background))
    if changed:
        variants.append(("transition-only", changed))

    results: list[dict] = []
    seen: set[tuple[str, tuple[int, ...]]] = set()
    for name, variant_rows in variants:
        frame_numbers = tuple(int(row["frame_number"]) for row in variant_rows)
        key = (name, frame_numbers)
        if not variant_rows or key in seen:
            continue
        seen.add(key)
        variant = strip_query_scene(scene, variant_rows, name)
        variant["_expected_status"] = (
            "identified"
            if any(row.get("frame_state") != "background_only" for row in variant_rows)
            else "unknown"
        )
        results.append(variant)
    return results


def evaluate(database: dict) -> dict:
    query = {
        "root": database.get("root"),
        "scenes": [variant for scene in database.get("scenes", []) for variant in build_variants(scene)],
    }
    report = identify(database, query)

    rows = []
    failures: list[str] = []
    min_margin = None
    min_ratio = None

    for row in report.get("rows", []):
        best = row.get("best_match") or {}
        second = row.get("second_match") or {}
        margin = float(row.get("score_margin") or 0.0)
        best_score = float(best.get("score") or 0.0)
        second_score = float(second.get("score") or 0.0)
        ratio = (best_score / second_score) if second_score > 0.0 else None
        query_label = row.get("query_scene_label")
        variant_name = "unknown"
        expected_scene_label = None
        expected_status = "identified"
        for scene in query.get("scenes", []):
            if scene.get("scene_label") == query_label:
                variant_name = scene.get("_query_variant", variant_name)
                expected_scene_label = scene.get("_expected_scene_label")
                expected_status = scene.get("_expected_status", expected_status)
                break

        if min_margin is None or margin < min_margin:
            min_margin = margin
        if ratio is not None and (min_ratio is None or ratio < min_ratio):
            min_ratio = ratio

        if row.get("identification_status") != expected_status:
            failures.append(f"{query_label}: expected status={expected_status}, got {row.get('identification_status')}")
        if expected_status == "identified" and best.get("scene_label") != expected_scene_label:
            failures.append(f"{query_label}: best_match={best.get('scene_label')}")
        if expected_status == "identified" and best.get("exact_scene_signature"):
            failures.append(f"{query_label}: partial query unexpectedly kept exact scene signature")
        if expected_status == "identified" and margin < 30.0:
            failures.append(f"{query_label}: margin too small ({margin:.6f})")
        if expected_status == "identified" and ratio is not None and ratio < 2.0:
            failures.append(f"{query_label}: ratio too small ({ratio:.6f})")

        rows.append(
            {
                "query_scene_label": query_label,
                "expected_scene_label": expected_scene_label,
                "expected_status": expected_status,
                "variant": variant_name,
                "status": row.get("identification_status"),
                "best_scene_label": best.get("scene_label"),
                "best_score": best.get("score"),
                "second_scene_label": second.get("scene_label"),
                "second_score": second.get("score"),
                "score_margin": row.get("score_margin"),
                "best_to_second_ratio": ratio,
                "shared_frame_count": best.get("shared_frame_count"),
                "exact_scene_signature": best.get("exact_scene_signature"),
            }
        )

    return {
        "rows": rows,
        "query_count": len(rows),
        "min_margin": min_margin,
        "min_best_to_second_ratio": min_ratio,
        "passed": not failures,
        "failures": failures,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Evaluate partial-timeline scene identification robustness")
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
