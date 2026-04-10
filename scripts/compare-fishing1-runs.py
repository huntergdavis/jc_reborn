#!/usr/bin/env python3
import argparse
import json
import subprocess
import sys
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser(
        description="Compare multiple FISHING 1 result bundles against labeled full-scene ground truth."
    )
    parser.add_argument(
        "--annotations",
        required=True,
        help="Path to fishing full-scene annotations.json",
    )
    parser.add_argument(
        "--result",
        action="append",
        required=True,
        help="Path to result.json or result directory. May be repeated.",
    )
    parser.add_argument(
        "--label",
        action="append",
        default=[],
        help="Optional human label for the preceding result. May be repeated.",
    )
    parser.add_argument(
        "--json-out",
        help="Optional path to write full comparison JSON",
    )
    return parser.parse_args()


def summarize(project_root: Path, annotations: Path, result: Path):
    script = project_root / "scripts" / "summarize-fishing1-result.py"
    proc = subprocess.run(
        ["python3", str(script), "--annotations", str(annotations), "--result", str(result)],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(proc.stdout)


def metric_delta(a, b, key):
    return (b.get("coverage") or {}).get(key, 0) - (a.get("coverage") or {}).get(key, 0)


def main():
    args = parse_args()
    project_root = Path(__file__).resolve().parents[1]
    annotations = Path(args.annotations).resolve()
    labels = list(args.label)
    while len(labels) < len(args.result):
        labels.append("")

    rows = []
    for idx, result_arg in enumerate(args.result):
        result_path = Path(result_arg).resolve()
        summary = summarize(project_root, annotations, result_path)
        summary["label"] = labels[idx] or result_path.name
        summary["result_path"] = str(result_path)
        rows.append(summary)

    deltas = []
    for prev, cur in zip(rows, rows[1:]):
        deltas.append(
            {
                "from": prev["label"],
                "to": cur["label"],
                "delta_unique_correct_hashes": metric_delta(prev, cur, "unique_correct_hashes"),
                "delta_unique_shoe_hashes": metric_delta(prev, cur, "unique_shoe_hashes"),
                "delta_unique_post_correct_pre_shoe_hashes": metric_delta(
                    prev, cur, "unique_post_correct_pre_shoe_hashes"
                ),
            }
        )

    report = {
        "annotations": str(annotations),
        "rows": rows,
        "deltas": deltas,
    }

    if args.json_out:
        out_path = Path(args.json_out)
        out_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    print("label\tregime\tcorrect\tshoe\tmidgap\tstate_hash")
    for row in rows:
        cov = row.get("coverage") or {}
        result = row.get("result") or {}
        print(
            "\t".join(
                [
                    row["label"],
                    row.get("regime", ""),
                    str(cov.get("unique_correct_hashes", 0)),
                    str(cov.get("unique_shoe_hashes", 0)),
                    str(cov.get("unique_post_correct_pre_shoe_hashes", 0)),
                    str(result.get("state_hash", "")),
                ]
            )
        )

    if deltas:
        print("")
        print("delta_from\tto\td_correct\td_shoe\td_midgap")
        for delta in deltas:
            print(
                "\t".join(
                    [
                        delta["from"],
                        delta["to"],
                        str(delta["delta_unique_correct_hashes"]),
                        str(delta["delta_unique_shoe_hashes"]),
                        str(delta["delta_unique_post_correct_pre_shoe_hashes"]),
                    ]
                )
            )


if __name__ == "__main__":
    main()
