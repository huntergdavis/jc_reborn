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


def nullable_int(value):
    if value is None:
        return 0
    return int(value)


def phase_mismatch_count(a, b, phase):
    a_rows = {row["frame"]: row["sha256"] for row in (a.get("phase_hashes") or {}).get(phase, [])}
    b_rows = {row["frame"]: row["sha256"] for row in (b.get("phase_hashes") or {}).get(phase, [])}
    mismatches = 0
    for frame in sorted(set(a_rows) | set(b_rows)):
        if a_rows.get(frame) != b_rows.get(frame):
            mismatches += 1
    return mismatches


def estimate_timeline_offset(a, b):
    phases = a.get("phase_hashes") or {}
    other_phases = b.get("phase_hashes") or {}
    candidate_offsets = set()
    for phase in sorted(set(phases) | set(other_phases)):
        a_rows = phases.get(phase) or []
        b_rows = other_phases.get(phase) or []
        a_by_hash = {}
        b_by_hash = {}
        for row in a_rows:
            a_by_hash.setdefault(row["sha256"], []).append(row["frame"])
        for row in b_rows:
            b_by_hash.setdefault(row["sha256"], []).append(row["frame"])
        for digest in sorted(set(a_by_hash) & set(b_by_hash)):
            for a_frame in a_by_hash[digest]:
                for b_frame in b_by_hash[digest]:
                    candidate_offsets.add(b_frame - a_frame)

    if not candidate_offsets:
        return {"best_offset": None, "matching_hash_pairs": 0, "offset_candidates": []}

    ranked = []
    for offset in sorted(candidate_offsets):
        matches = 0
        for phase in sorted(set(phases) | set(other_phases)):
            a_rows = {row["frame"]: row["sha256"] for row in (phases.get(phase) or [])}
            b_rows = {row["frame"]: row["sha256"] for row in (other_phases.get(phase) or [])}
            for frame, digest in a_rows.items():
                if b_rows.get(frame + offset) == digest:
                    matches += 1
        ranked.append((offset, matches))

    ranked.sort(key=lambda item: (-item[1], abs(item[0]), item[0]))
    return {
        "best_offset": ranked[0][0],
        "matching_hash_pairs": ranked[0][1],
        "offset_candidates": [
            {"offset": offset, "matching_hash_pairs": count}
            for offset, count in ranked[:10]
        ],
    }


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
        offset = estimate_timeline_offset(prev, cur)
        deltas.append(
            {
                "from": prev["label"],
                "to": cur["label"],
                "timeline_offset": offset,
                "delta_unique_correct_hashes": metric_delta(prev, cur, "unique_correct_hashes"),
                "delta_unique_shoe_hashes": metric_delta(prev, cur, "unique_shoe_hashes"),
                "delta_unique_post_correct_pre_shoe_hashes": metric_delta(
                    prev, cur, "unique_post_correct_pre_shoe_hashes"
                ),
                "black_phase_mismatches": phase_mismatch_count(prev, cur, "black"),
                "ocean_phase_mismatches": phase_mismatch_count(prev, cur, "ocean_only"),
                "island_phase_mismatches": phase_mismatch_count(prev, cur, "island_only"),
                "correct_phase_mismatches": phase_mismatch_count(prev, cur, "correct"),
                "shoe_phase_mismatches": phase_mismatch_count(prev, cur, "shoe_only"),
                "midgap_phase_mismatches": phase_mismatch_count(prev, cur, "post_correct_pre_shoe"),
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

    print(
        "label\tboot_string\texe_sha256\tregime\tstartup_regime\tfirst_visible\tfirst_lower_half\tfirst_full_height\tlast_partial_height\tlast_black\t"
        "black\tocean\tisland\tcorrect\tshoe\tmidgap\tstate_hash"
    )
    for row in rows:
        cov = row.get("coverage") or {}
        result = row.get("result") or {}
        scene = row.get("scene") or {}
        build = row.get("build") or {}
        artifacts = build.get("artifacts") or {}
        print(
            "\t".join(
                [
                    row["label"],
                    str(scene.get("boot_string", "")),
                    str(artifacts.get("exe_sha256", "")),
                    row.get("regime", ""),
                    row.get("startup_regime", ""),
                    str(cov.get("first_visible_frame", "")),
                    str(cov.get("first_lower_half_visible_frame", "")),
                    str(cov.get("first_full_height_visible_frame", "")),
                    str(cov.get("last_partial_height_visible_frame", "")),
                    str(cov.get("last_black_frame", "")),
                    str(cov.get("unique_black_hashes", 0)),
                    str(cov.get("unique_ocean_only_hashes", 0)),
                    str(cov.get("unique_island_only_hashes", 0)),
                    str(cov.get("unique_correct_hashes", 0)),
                    str(cov.get("unique_shoe_hashes", 0)),
                    str(cov.get("unique_post_correct_pre_shoe_hashes", 0)),
                    str(result.get("state_hash", "")),
                ]
            )
        )

    if deltas:
        print("")
        print(
            "delta_from\tto\tboot_changed\texe_changed\tstartup_from\tstartup_to\tstartup_changed\td_first_visible\td_first_lower_half\td_first_full_height\td_last_partial_height\t"
            "d_last_black\tbest_timeline_offset\toffset_hash_pairs\td_black\td_ocean\td_island\td_correct\td_shoe\td_midgap\t"
            "m_black\tm_ocean\tm_island\tm_correct\tm_shoe\tm_midgap"
        )
        for delta in deltas:
            prev_row = next(row for row in rows if row["label"] == delta["from"])
            cur_row = next(row for row in rows if row["label"] == delta["to"])
            prev_scene = prev_row.get("scene") or {}
            cur_scene = cur_row.get("scene") or {}
            prev_artifacts = (prev_row.get("build") or {}).get("artifacts") or {}
            cur_artifacts = (cur_row.get("build") or {}).get("artifacts") or {}
            timeline_offset = delta.get("timeline_offset") or {}
            print(
                "\t".join(
                    [
                        delta["from"],
                        delta["to"],
                        "1" if prev_scene.get("boot_string") != cur_scene.get("boot_string") else "0",
                        "1" if prev_artifacts.get("exe_sha256") != cur_artifacts.get("exe_sha256") else "0",
                        prev_row.get("startup_regime", ""),
                        cur_row.get("startup_regime", ""),
                        "1" if prev_row.get("startup_regime") != cur_row.get("startup_regime") else "0",
                        str(
                            nullable_int((cur_row.get("coverage") or {}).get("first_visible_frame"))
                            - nullable_int((prev_row.get("coverage") or {}).get("first_visible_frame"))
                        ),
                        str(
                            nullable_int((cur_row.get("coverage") or {}).get("first_lower_half_visible_frame"))
                            - nullable_int((prev_row.get("coverage") or {}).get("first_lower_half_visible_frame"))
                        ),
                        str(
                            nullable_int((cur_row.get("coverage") or {}).get("first_full_height_visible_frame"))
                            - nullable_int((prev_row.get("coverage") or {}).get("first_full_height_visible_frame"))
                        ),
                        str(
                            nullable_int((cur_row.get("coverage") or {}).get("last_partial_height_visible_frame"))
                            - nullable_int((prev_row.get("coverage") or {}).get("last_partial_height_visible_frame"))
                        ),
                        str(
                            nullable_int((cur_row.get("coverage") or {}).get("last_black_frame"))
                            - nullable_int((prev_row.get("coverage") or {}).get("last_black_frame"))
                        ),
                        str(timeline_offset.get("best_offset", "")),
                        str(timeline_offset.get("matching_hash_pairs", 0)),
                        str(metric_delta(prev_row, cur_row, "unique_black_hashes")),
                        str(metric_delta(prev_row, cur_row, "unique_ocean_only_hashes")),
                        str(metric_delta(prev_row, cur_row, "unique_island_only_hashes")),
                        str(delta["delta_unique_correct_hashes"]),
                        str(delta["delta_unique_shoe_hashes"]),
                        str(delta["delta_unique_post_correct_pre_shoe_hashes"]),
                        str(delta["black_phase_mismatches"]),
                        str(delta["ocean_phase_mismatches"]),
                        str(delta["island_phase_mismatches"]),
                        str(delta["correct_phase_mismatches"]),
                        str(delta["shoe_phase_mismatches"]),
                        str(delta["midgap_phase_mismatches"]),
                    ]
                )
            )


if __name__ == "__main__":
    main()
