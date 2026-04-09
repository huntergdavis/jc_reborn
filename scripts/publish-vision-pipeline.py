#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
import csv
import os


def write_json(path: Path, data: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def rel_to(root: Path, target: Path) -> str:
    return target.resolve().relative_to(root.resolve()).as_posix() if target.resolve().is_relative_to(root.resolve()) else target.resolve().as_posix()


def href_from(html_path: Path, target: Path) -> str:
    return os.path.relpath(target.resolve(), html_path.parent.resolve())


def main() -> None:
    project_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Publish a portable vision pipeline bundle.")
    parser.add_argument(
        "--bankdir",
        type=Path,
        default=project_root / "vision-artifacts" / "vision-reference-pipeline-current" / "reference-bank",
    )
    parser.add_argument(
        "--selfcheckdir",
        type=Path,
        default=project_root / "vision-artifacts" / "vision-reference-selfcheck-20260329-v4",
    )
    parser.add_argument(
        "--outroot",
        type=Path,
        default=project_root / "vision-artifacts" / "vision-reference-pipeline-current",
    )
    args = parser.parse_args()

    bankdir = args.bankdir.resolve()
    selfcheckdir = args.selfcheckdir.resolve()
    outroot = args.outroot.resolve()

    outroot.mkdir(parents=True, exist_ok=True)

    bank = json.loads((bankdir / "index.json").read_text())
    selfcheck = json.loads((selfcheckdir / "index.json").read_text())
    quality = json.loads((selfcheckdir / "quality-report.json").read_text())
    confusion = json.loads((selfcheckdir / "confusion-report.json").read_text())
    family = json.loads((selfcheckdir / "family-report.json").read_text())

    quality_by_scene = {row["scene_id"]: row for row in quality["scenes"]}
    confusion_by_scene = {row["scene_id"]: row for row in confusion["scenes"]}

    inventory_rows = []
    for row in selfcheck["scenes"]:
        scene_id = row["scene_id"]
        q = quality_by_scene[scene_id]
        c = confusion_by_scene[scene_id]
        inventory_rows.append(
            {
                "scene_id": scene_id,
                "family": row["family"],
                "frame_count": row["frame_count"],
                "review_html": rel_to(outroot, selfcheckdir / row["review_html"]),
                "vision_analysis_json": rel_to(outroot, selfcheckdir / "scenes" / scene_id / "vision-analysis.json"),
                "expected_top1_ratio": q["expected_top1_ratio"],
                "global_top1_ratio": q["global_top1_ratio"],
                "sprite_visible_ratio": q["sprite_visible_ratio"],
                "ocean_ratio": q["ocean_ratio"],
                "dominant_global_match_scene": q["dominant_global_match_scene"],
                "dominant_failure_mode": q["dominant_failure_mode"],
                "top_alternates": c["alternates"][:3],
            }
        )

    manifest = {
        "reference_bank": {
            "index_html": rel_to(outroot, bankdir / "index.html"),
            "index_json": rel_to(outroot, bankdir / "index.json"),
            "metadata_json": rel_to(outroot, bankdir / "metadata.json"),
            "features_npy": rel_to(outroot, bankdir / "features.npy"),
            "scene_count": len(bank["scenes"]),
            "frame_count": bank["frame_count"],
        },
        "reference_selfcheck": {
            "index_html": rel_to(outroot, selfcheckdir / "index.html"),
            "index_json": rel_to(outroot, selfcheckdir / "index.json"),
            "quality_html": rel_to(outroot, selfcheckdir / "quality-report.html"),
            "quality_json": rel_to(outroot, selfcheckdir / "quality-report.json"),
            "confusion_html": rel_to(outroot, selfcheckdir / "confusion-report.html"),
            "confusion_json": rel_to(outroot, selfcheckdir / "confusion-report.json"),
            "family_html": rel_to(outroot, selfcheckdir / "family-report.html"),
            "family_json": rel_to(outroot, selfcheckdir / "family-report.json"),
            "scene_count": selfcheck["scene_count"],
        },
        "inventory_json": "scene-inventory.json",
        "inventory_html": "scene-inventory.html",
        "artifact_catalog_json": "artifact-catalog.json",
    }
    write_json(outroot / "pipeline-manifest.json", manifest)
    write_json(outroot / "scene-inventory.json", {"scenes": inventory_rows, "scene_count": len(inventory_rows)})

    with (outroot / "scene-inventory.csv").open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(
            fh,
            fieldnames=[
                "scene_id",
                "family",
                "frame_count",
                "review_html",
                "vision_analysis_json",
                "expected_top1_ratio",
                "global_top1_ratio",
                "sprite_visible_ratio",
                "ocean_ratio",
                "dominant_global_match_scene",
                "dominant_failure_mode",
                "top_alternates",
            ],
        )
        writer.writeheader()
        for row in inventory_rows:
            writer.writerow(
                {
                    **row,
                    "top_alternates": "; ".join(
                        f"{a['scene_id']}:{a['ratio']:.3f}" for a in row["top_alternates"]
                    ),
                }
            )

    with (outroot / "family-summary.csv").open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(
            fh,
            fieldnames=[
                "family",
                "scene_count",
                "avg_expected_top1",
                "avg_global_top1",
                "avg_sprite_visible",
                "avg_ocean_ratio",
            ],
        )
        writer.writeheader()
        for row in family["families"]:
            writer.writerow(row)

    strongest = sorted(inventory_rows, key=lambda r: (-r["global_top1_ratio"], r["scene_id"]))[:20]
    weakest = sorted(inventory_rows, key=lambda r: (r["global_top1_ratio"], r["scene_id"]))[:20]
    write_json(outroot / "strongest-scenes.json", {"scenes": strongest})
    write_json(outroot / "weakest-scenes.json", {"scenes": weakest})

    pair_map: dict[tuple[str, str], dict[str, object]] = {}
    for row in inventory_rows:
        src = row["scene_id"]
        for alt in row["top_alternates"]:
            pair = (src, alt["scene_id"])
            pair_map[pair] = {
                "source_scene": src,
                "target_scene": alt["scene_id"],
                "ratio": alt["ratio"],
                "family_source": row["family"],
                "family_target": alt["scene_id"].split("-", 1)[0],
                "source_review_html": row["review_html"],
            }
    confusion_pairs = sorted(pair_map.values(), key=lambda r: (-float(r["ratio"]), str(r["source_scene"]), str(r["target_scene"])))
    write_json(outroot / "top-confusion-pairs.json", {"pairs": confusion_pairs[:50]})

    artifact_catalog = {
        "top_level": {
            "pipeline_index_html": "index.html",
            "pipeline_manifest_json": "pipeline-manifest.json",
            "scene_inventory_html": "scene-inventory.html",
            "scene_inventory_json": "scene-inventory.json",
            "scene_inventory_csv": "scene-inventory.csv",
            "strongest_scenes_json": "strongest-scenes.json",
            "weakest_scenes_json": "weakest-scenes.json",
            "top_confusion_pairs_json": "top-confusion-pairs.json",
            "validation_report_html": "validation-report.html",
            "validation_report_json": "validation-report.json",
        },
        "reference_bank": {
            "index_html": rel_to(outroot, bankdir / "index.html"),
            "index_json": rel_to(outroot, bankdir / "index.json"),
            "metadata_json": rel_to(outroot, bankdir / "metadata.json"),
            "features_npy": rel_to(outroot, bankdir / "features.npy"),
        },
        "reference_selfcheck": {
            "index_html": rel_to(outroot, selfcheckdir / "index.html"),
            "index_json": rel_to(outroot, selfcheckdir / "index.json"),
            "quality_report_html": rel_to(outroot, selfcheckdir / "quality-report.html"),
            "quality_report_json": rel_to(outroot, selfcheckdir / "quality-report.json"),
            "confusion_report_html": rel_to(outroot, selfcheckdir / "confusion-report.html"),
            "confusion_report_json": rel_to(outroot, selfcheckdir / "confusion-report.json"),
            "family_report_html": rel_to(outroot, selfcheckdir / "family-report.html"),
            "family_report_json": rel_to(outroot, selfcheckdir / "family-report.json"),
        },
        "scenes": [
            {
                "scene_id": row["scene_id"],
                "family": row["family"],
                "review_html": row["review_html"],
                "vision_analysis_json": row["vision_analysis_json"],
            }
            for row in inventory_rows
        ],
    }
    write_json(outroot / "artifact-catalog.json", artifact_catalog)
    catalog_rows = []
    for section, entries in artifact_catalog.items():
        if isinstance(entries, dict):
            for name, value in entries.items():
                catalog_rows.append((section, name, value))
        elif isinstance(entries, list):
            catalog_rows.append((section, "count", len(entries)))

    best_rows = sorted(inventory_rows, key=lambda r: (-r["global_top1_ratio"], r["scene_id"]))[:15]
    weak_rows = sorted(inventory_rows, key=lambda r: (r["global_top1_ratio"], r["scene_id"]))[:20]

    def row_html(row: dict) -> str:
        return (
            f"<tr><td><a href=\"{row['review_html']}\">{row['scene_id']}</a></td>"
            f"<td>{row['family']}</td>"
            f"<td>{row['global_top1_ratio']:.3f}</td>"
            f"<td>{row['expected_top1_ratio']:.3f}</td>"
            f"<td>{row['sprite_visible_ratio']:.3f}</td>"
            f"<td>{row['ocean_ratio']:.3f}</td>"
            f"<td>{row['dominant_global_match_scene']}</td>"
            f"<td>{row['dominant_failure_mode']}</td></tr>"
        )

    index_html_path = outroot / "index.html"
    html = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Vision Reference Pipeline</title>
  <style>
    body {{ font-family: sans-serif; margin: 24px; }}
    table {{ border-collapse: collapse; width: 100%; margin-bottom: 24px; }}
    td, th {{ border: 1px solid #ccc; padding: 8px; text-align: left; }}
    .links a {{ display: block; margin: 6px 0; }}
  </style>
</head>
<body>
  <h1>Vision Reference Pipeline</h1>
  <div class="links">
    <a href="{href_from(index_html_path, bankdir / 'index.html')}">Reference bank index</a>
    <a href="{href_from(index_html_path, selfcheckdir / 'index.html')}">Reference self-check index</a>
    <a href="{href_from(index_html_path, selfcheckdir / 'quality-report.html')}">Quality report</a>
    <a href="{href_from(index_html_path, selfcheckdir / 'confusion-report.html')}">Confusion report</a>
    <a href="{href_from(index_html_path, selfcheckdir / 'family-report.html')}">Family report</a>
    <a href="{href_from(index_html_path, outroot / 'scene-inventory.html')}">Scene inventory</a>
    <a href="{href_from(index_html_path, outroot / 'scene-inventory.csv')}">Scene inventory CSV</a>
    <a href="{href_from(index_html_path, outroot / 'family-summary.csv')}">Family summary CSV</a>
    <a href="{href_from(index_html_path, outroot / 'strongest-scenes.json')}">Strongest scenes JSON</a>
    <a href="{href_from(index_html_path, outroot / 'weakest-scenes.json')}">Weakest scenes JSON</a>
    <a href="{href_from(index_html_path, outroot / 'top-confusion-pairs.json')}">Top confusion pairs JSON</a>
    <a href="{href_from(index_html_path, outroot / 'artifact-catalog.json')}">Artifact catalog JSON</a>
    <a href="{href_from(index_html_path, outroot / 'artifact-catalog.html')}">Artifact catalog HTML</a>
    <a href="{href_from(index_html_path, outroot / 'pipeline-manifest.json')}">Manifest JSON</a>
  </div>
  <p>Reference scenes: {len(bank['scenes'])}. Reference frames: {bank['frame_count']}.</p>
  <h2>Top Global Self-Matches</h2>
  <table>
    <thead><tr><th>Scene</th><th>Family</th><th>Global Top1</th><th>Expected Top1</th><th>Sprites</th><th>Ocean</th><th>Dominant Global</th><th>Failure</th></tr></thead>
    <tbody>{''.join(row_html(r) for r in best_rows)}</tbody>
  </table>
  <h2>Weakest Scenes</h2>
  <table>
    <thead><tr><th>Scene</th><th>Family</th><th>Global Top1</th><th>Expected Top1</th><th>Sprites</th><th>Ocean</th><th>Dominant Global</th><th>Failure</th></tr></thead>
    <tbody>{''.join(row_html(r) for r in weak_rows)}</tbody>
  </table>
  <h2>Top Confusion Pairs</h2>
  <table>
    <thead><tr><th>Source</th><th>Target</th><th>Ratio</th><th>Source Family</th><th>Target Family</th></tr></thead>
    <tbody>
      {''.join(
          f"<tr><td><a href=\"{pair['source_review_html']}\">{pair['source_scene']}</a></td><td>{pair['target_scene']}</td><td>{float(pair['ratio']):.3f}</td><td>{pair['family_source']}</td><td>{pair['family_target']}</td></tr>"
          for pair in confusion_pairs[:20]
      )}
    </tbody>
  </table>
</body>
</html>
"""
    index_html_path.write_text(html, encoding="utf-8")

    catalog_html = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Vision Artifact Catalog</title>
  <style>
    body {{ font-family: sans-serif; margin: 24px; }}
    table {{ border-collapse: collapse; width: 100%; }}
    td, th {{ border: 1px solid #ccc; padding: 8px; text-align: left; }}
  </style>
</head>
<body>
  <h1>Vision Artifact Catalog</h1>
  <table>
    <thead><tr><th>Section</th><th>Name</th><th>Value</th></tr></thead>
    <tbody>
      {''.join(f"<tr><td>{section}</td><td>{name}</td><td>{value}</td></tr>" for section, name, value in catalog_rows)}
    </tbody>
  </table>
</body>
</html>
"""
    (outroot / "artifact-catalog.html").write_text(catalog_html, encoding="utf-8")

    inv_rows = []
    for row in inventory_rows:
        alts = ", ".join(f"{a['scene_id']} ({a['ratio']:.3f})" for a in row["top_alternates"]) or "-"
        inv_rows.append(
            f"<tr><td><a href=\"{row['review_html']}\">{row['scene_id']}</a></td>"
            f"<td>{row['family']}</td>"
            f"<td>{row['frame_count']}</td>"
            f"<td>{row['global_top1_ratio']:.3f}</td>"
            f"<td>{row['expected_top1_ratio']:.3f}</td>"
            f"<td>{row['sprite_visible_ratio']:.3f}</td>"
            f"<td>{row['ocean_ratio']:.3f}</td>"
            f"<td>{row['dominant_global_match_scene']}</td>"
            f"<td>{row['dominant_failure_mode']}</td>"
            f"<td>{alts}</td></tr>"
        )
    inv_html = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Vision Scene Inventory</title>
  <style>
    body {{ font-family: sans-serif; margin: 24px; }}
    table {{ border-collapse: collapse; width: 100%; }}
    td, th {{ border: 1px solid #ccc; padding: 8px; text-align: left; }}
  </style>
</head>
<body>
  <h1>Vision Scene Inventory</h1>
  <table>
    <thead><tr><th>Scene</th><th>Family</th><th>Frames</th><th>Global Top1</th><th>Expected Top1</th><th>Sprites</th><th>Ocean</th><th>Dominant Global</th><th>Failure</th><th>Top Alternates</th></tr></thead>
    <tbody>{''.join(inv_rows)}</tbody>
  </table>
</body>
</html>
"""
    (outroot / "scene-inventory.html").write_text(inv_html, encoding="utf-8")


if __name__ == "__main__":
    main()
