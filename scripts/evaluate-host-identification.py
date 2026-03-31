#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path


def evaluate(report: dict) -> dict:
    rows = []
    min_identified_margin = None
    max_nonmatch_score = None
    min_nonmatch_margin = None
    min_best_to_second_ratio = None
    failures: list[str] = []

    for row in report.get("rows", []):
        query = row.get("query_scene_label")
        status = row.get("identification_status")
        best = row.get("best_match") or {}
        second = row.get("second_match") or {}
        margin = float(row.get("score_margin") or 0.0)
        best_score = float(best.get("score") or 0.0)
        second_score = float(second.get("score") or 0.0)
        if second_score > 0.0:
            best_to_second_ratio = best_score / second_score
        elif best_score > 0.0:
            best_to_second_ratio = None
        else:
            best_to_second_ratio = 0.0

        if status != "identified":
            failures.append(f"{query}: status={status}")
        if not best.get("exact_scene_signature"):
            failures.append(f"{query}: best match lacks exact scene signature")

        if min_identified_margin is None or margin < min_identified_margin:
            min_identified_margin = margin
        if max_nonmatch_score is None or second_score > max_nonmatch_score:
            max_nonmatch_score = second_score
        if min_nonmatch_margin is None or margin < min_nonmatch_margin:
            min_nonmatch_margin = margin
        if best_to_second_ratio is not None and (
            min_best_to_second_ratio is None or best_to_second_ratio < min_best_to_second_ratio
        ):
            min_best_to_second_ratio = best_to_second_ratio

        if margin < 100.0:
            failures.append(f"{query}: identification margin too small ({margin:.6f})")
        if second_score > 80.0:
            failures.append(f"{query}: nonmatch score too high ({second_score:.6f})")
        if best_to_second_ratio is not None and best_to_second_ratio < 3.0:
            failures.append(f"{query}: best/second ratio too small ({best_to_second_ratio:.6f})")

        rows.append(
            {
                "query_scene_label": query,
                "status": status,
                "best_scene_label": best.get("scene_label"),
                "best_score": best.get("score"),
                "second_scene_label": second.get("scene_label"),
                "second_score": second.get("score"),
                "score_margin": row.get("score_margin"),
                "best_to_second_ratio": best_to_second_ratio,
            }
        )

    return {
        "rows": rows,
        "scene_count": len(rows),
        "min_identified_margin": min_identified_margin,
        "max_nonmatch_score": max_nonmatch_score,
        "min_nonmatch_margin": min_nonmatch_margin,
        "min_best_to_second_ratio": min_best_to_second_ratio,
        "passed": not failures,
        "failures": failures,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Evaluate semantic scene identification confusion risk")
    parser.add_argument("--report-json", type=Path, required=True)
    parser.add_argument("--out-json", type=Path)
    args = parser.parse_args()

    report = json.loads(args.report_json.read_text(encoding="utf-8"))
    payload = json.dumps(evaluate(report), indent=2) + "\n"
    if args.out_json:
        args.out_json.write_text(payload, encoding="utf-8")
    else:
        print(payload, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
