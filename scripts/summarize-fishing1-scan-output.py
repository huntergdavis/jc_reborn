#!/usr/bin/env python3
import argparse
import json
import subprocess
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser(
        description=(
            "Summarize all completed FISHING 1 result.json files under a scan output "
            "directory and emit summary/boundary reports."
        )
    )
    parser.add_argument("--output-root", required=True, help="Scan output directory to inspect")
    parser.add_argument("--annotations", required=True, help="Full-scene FISHING 1 annotations.json")
    parser.add_argument(
        "--skipped-dir-name",
        action="append",
        default=[],
        help="Exact binary-library dir names skipped during the scan. May be repeated.",
    )
    parser.add_argument(
        "--boundary-startup-regime",
        help="Optional startup regime to resolve into fishing1-startup-boundary.json",
    )
    parser.add_argument(
        "--boundary-min-first-visible",
        type=int,
        help="Optional first-visible threshold to resolve into fishing1-visibility-boundary.json",
    )
    parser.add_argument(
        "--boundary-min-first-full-height",
        type=int,
        help="Optional first-full-height threshold to resolve into fishing1-visibility-boundary.json",
    )
    return parser.parse_args()


def summarize_results(project_root: Path, output_root: Path, annotations: Path, skipped_dir_names: list[str]):
    summarizer = project_root / "scripts" / "summarize-fishing1-result.py"
    rows = []
    for result_path in sorted(output_root.rglob("result.json")):
        if result_path.parent == output_root:
            continue
        proc = subprocess.run(
            ["python3", str(summarizer), "--annotations", str(annotations), "--result", str(result_path)],
            check=True,
            capture_output=True,
            text=True,
        )
        summary = json.loads(proc.stdout)
        summary["result_json"] = str(result_path)
        rows.append(summary)

    report = {
        "annotations": str(annotations),
        "output_root": str(output_root),
        "rows": rows,
        "skipped_dir_names": skipped_dir_names,
    }
    report_path = output_root / "fishing1-binary-regression-summary.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report_path, rows


def write_startup_tsv(summary_path: Path, rows):
    out_path = summary_path.with_name("fishing1-startup-summary.tsv")
    with out_path.open("w", encoding="utf-8") as handle:
        handle.write(
            "build\tstartup_regime\tfirst_visible\tfirst_lower_half\tfirst_full_height\t"
            "last_partial_height\tlast_black\tblack\tocean\tisland\tcorrect\tshoe\tmidgap\tstate_hash\n"
        )
        for row in rows:
            cov = row.get("coverage") or {}
            result = row.get("result") or {}
            build = Path(row.get("result_json", "")).parts[-2] if row.get("result_json") else ""
            fields = [
                build,
                row.get("startup_regime", ""),
                str(cov.get("first_visible_frame", "")),
                str(cov.get("first_lower_half_visible_frame", "")),
                str(cov.get("first_full_height_visible_frame", "")),
                str(cov.get("last_partial_height_visible_frame", "")),
                str(cov.get("last_black_frame", "")),
                str(cov.get("unique_black_hashes", "")),
                str(cov.get("unique_ocean_only_hashes", "")),
                str(cov.get("unique_island_only_hashes", "")),
                str(cov.get("unique_correct_hashes", "")),
                str(cov.get("unique_shoe_hashes", "")),
                str(cov.get("unique_post_correct_pre_shoe_hashes", "")),
                str(result.get("state_hash", "")),
            ]
            handle.write("\t".join(fields) + "\n")
    return out_path


def write_boundary(project_root: Path, summary_path: Path, startup_regime: str):
    boundary_path = summary_path.with_name("fishing1-startup-boundary.json")
    finder = project_root / "scripts" / "find-fishing1-regression-boundary.py"
    with boundary_path.open("w", encoding="utf-8") as handle:
        subprocess.run(
            ["python3", str(finder), "--summary-json", str(summary_path), "--startup-regime", startup_regime],
            check=True,
            text=True,
            stdout=handle,
        )
    return boundary_path


def write_visibility_boundary(project_root: Path, summary_path: Path, min_first_visible, min_first_full_height):
    boundary_path = summary_path.with_name("fishing1-visibility-boundary.json")
    finder = project_root / "scripts" / "find-fishing1-regression-boundary.py"
    cmd = ["python3", str(finder), "--summary-json", str(summary_path)]
    if min_first_visible is not None:
        cmd.extend(["--min-first-visible", str(min_first_visible)])
    if min_first_full_height is not None:
        cmd.extend(["--min-first-full-height", str(min_first_full_height)])
    with boundary_path.open("w", encoding="utf-8") as handle:
        subprocess.run(cmd, check=True, text=True, stdout=handle)
    return boundary_path


def main():
    args = parse_args()
    project_root = Path(__file__).resolve().parent.parent
    output_root = Path(args.output_root).resolve()
    annotations = Path(args.annotations).resolve()

    summary_path, rows = summarize_results(project_root, output_root, annotations, args.skipped_dir_name)
    print(summary_path)
    print(write_startup_tsv(summary_path, rows))
    if args.boundary_startup_regime:
        print(write_boundary(project_root, summary_path, args.boundary_startup_regime))
    if args.boundary_min_first_visible is not None or args.boundary_min_first_full_height is not None:
        print(
            write_visibility_boundary(
                project_root,
                summary_path,
                args.boundary_min_first_visible,
                args.boundary_min_first_full_height,
            )
        )


if __name__ == "__main__":
    main()
