#!/usr/bin/env python3
from __future__ import annotations

import json
import argparse
from pathlib import Path


def write_json(path: Path, data: object) -> None:
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def resolve_artifact_path(path_value: str, base_dir: Path) -> Path:
    path = Path(path_value)
    if path.is_absolute():
        return path
    return (base_dir / path).resolve()


def main() -> None:
    project_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Validate a published vision pipeline bundle.")
    parser.add_argument("--root", type=Path, help="Pipeline bundle root containing pipeline-manifest.json")
    parser.add_argument("--manifest-json", type=Path, help="Direct path to pipeline-manifest.json")
    args = parser.parse_args()

    if args.manifest_json:
        manifest_path = args.manifest_json.resolve()
        root = manifest_path.parent
    elif args.root:
        root = args.root.resolve()
        manifest_path = root / "pipeline-manifest.json"
    else:
        root = project_root / "vision-artifacts" / "vision-reference-pipeline-current"
        manifest_path = root / "pipeline-manifest.json"

    manifest = json.loads(manifest_path.read_text())

    bankdir = resolve_artifact_path(manifest["reference_bank"]["index_html"], manifest_path.parent).parent
    selfcheckdir = resolve_artifact_path(manifest["reference_selfcheck"]["index_html"], manifest_path.parent).parent

    checks = []

    def add_check(name: str, ok: bool, detail: str) -> None:
        checks.append({"name": name, "ok": ok, "detail": detail})

    bank_index = json.loads((bankdir / "index.json").read_text())
    selfcheck_index = json.loads((selfcheckdir / "index.json").read_text())
    inventory_path = root / "scene-inventory.json"
    inventory = json.loads(inventory_path.read_text())
    catalog_path = root / "artifact-catalog.json"
    catalog = json.loads(catalog_path.read_text()) if catalog_path.exists() else None

    bank_scene_count = len(bank_index["scenes"])
    selfcheck_scene_count = int(selfcheck_index["scene_count"])
    inventory_scene_count = int(inventory["scene_count"])
    bank_scene_ids = {str(row["scene_id"]) for row in bank_index["scenes"]}
    selfcheck_scene_ids = {str(row["scene_id"]) for row in selfcheck_index.get("scenes", [])}
    inventory_scene_ids = {str(row["scene_id"]) for row in inventory.get("scenes", [])}
    catalog_scene_ids = {str(row["scene_id"]) for row in (catalog or {}).get("scenes", [])}
    add_check(
        "scene_count_consistent",
        bank_scene_count == selfcheck_scene_count == inventory_scene_count,
        f"bank={bank_scene_count}, selfcheck={selfcheck_scene_count}, inventory={inventory_scene_count}",
    )
    add_check(
        "scene_ids_consistent",
        bank_scene_ids == selfcheck_scene_ids == inventory_scene_ids,
        (
            f"bank_only={sorted(bank_scene_ids - selfcheck_scene_ids - inventory_scene_ids)[:5]}, "
            f"selfcheck_only={sorted(selfcheck_scene_ids - bank_scene_ids - inventory_scene_ids)[:5]}, "
            f"inventory_only={sorted(inventory_scene_ids - bank_scene_ids - selfcheck_scene_ids)[:5]}"
        ),
    )
    add_check(
        "artifact_catalog_scene_ids_consistent",
        catalog is not None and catalog_scene_ids == inventory_scene_ids,
        (
            "artifact-catalog.json missing"
            if catalog is None else
            f"catalog_only={sorted(catalog_scene_ids - inventory_scene_ids)[:5]}, "
            f"inventory_only={sorted(inventory_scene_ids - catalog_scene_ids)[:5]}"
        ),
    )
    add_check("bank_features_exists", (bankdir / "features.npy").exists(), str(bankdir / "features.npy"))
    add_check("bank_metadata_exists", (bankdir / "metadata.json").exists(), str(bankdir / "metadata.json"))
    add_check("quality_report_exists", (selfcheckdir / "quality-report.html").exists(), str(selfcheckdir / "quality-report.html"))
    add_check("confusion_report_exists", (selfcheckdir / "confusion-report.html").exists(), str(selfcheckdir / "confusion-report.html"))
    add_check("family_report_exists", (selfcheckdir / "family-report.html").exists(), str(selfcheckdir / "family-report.html"))
    add_check("scene_inventory_html_exists", (root / "scene-inventory.html").exists(), str(root / "scene-inventory.html"))
    add_check("scene_inventory_csv_exists", (root / "scene-inventory.csv").exists(), str(root / "scene-inventory.csv"))

    missing_reviews = []
    missing_json = []
    for scene in inventory["scenes"]:
        review = resolve_artifact_path(scene["review_html"], inventory_path.parent)
        analysis = resolve_artifact_path(scene["vision_analysis_json"], inventory_path.parent)
        if not review.exists():
            missing_reviews.append(scene["scene_id"])
        if not analysis.exists():
            missing_json.append(scene["scene_id"])
    add_check("per_scene_reviews_exist", not missing_reviews, ", ".join(missing_reviews[:10]) or "all present")
    add_check("per_scene_analysis_exist", not missing_json, ", ".join(missing_json[:10]) or "all present")

    passed = sum(1 for c in checks if c["ok"])
    overall_ok = passed == len(checks)
    result = {
        "overall_ok": overall_ok,
        "checks_passed": passed,
        "checks_total": len(checks),
        "checks": checks,
    }
    write_json(root / "validation-report.json", result)

    rows = []
    for c in checks:
        rows.append(
            f"<tr><td>{c['name']}</td><td>{'PASS' if c['ok'] else 'FAIL'}</td><td>{c['detail']}</td></tr>"
        )

    html = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Vision Pipeline Validation</title>
  <style>
    body {{ font-family: sans-serif; margin: 24px; }}
    table {{ border-collapse: collapse; width: 100%; }}
    td, th {{ border: 1px solid #ccc; padding: 8px; text-align: left; }}
  </style>
</head>
<body>
  <h1>Vision Pipeline Validation</h1>
  <p>Overall: {'PASS' if overall_ok else 'FAIL'} ({passed}/{len(checks)})</p>
  <table>
    <thead><tr><th>Check</th><th>Status</th><th>Detail</th></tr></thead>
    <tbody>{''.join(rows)}</tbody>
  </table>
</body>
</html>
"""
    (root / "validation-report.html").write_text(html, encoding="utf-8")


if __name__ == "__main__":
    main()
