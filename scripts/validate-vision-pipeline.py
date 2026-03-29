#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path


def write_json(path: Path, data: object) -> None:
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    root = Path("/home/hunter/workspace/jc_reborn/vision-artifacts/vision-reference-pipeline-current")
    manifest = json.loads((root / "pipeline-manifest.json").read_text())

    bankdir = Path(manifest["reference_bank"]["index_html"]).parent
    selfcheckdir = Path(manifest["reference_selfcheck"]["index_html"]).parent

    checks = []

    def add_check(name: str, ok: bool, detail: str) -> None:
        checks.append({"name": name, "ok": ok, "detail": detail})

    bank_index = json.loads((bankdir / "index.json").read_text())
    selfcheck_index = json.loads((selfcheckdir / "index.json").read_text())
    inventory = json.loads((root / "scene-inventory.json").read_text())

    add_check("bank_scene_count", len(bank_index["scenes"]) == 63, f"{len(bank_index['scenes'])} scenes")
    add_check("selfcheck_scene_count", selfcheck_index["scene_count"] == 63, f"{selfcheck_index['scene_count']} scenes")
    add_check("inventory_scene_count", inventory["scene_count"] == 63, f"{inventory['scene_count']} scenes")
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
        review = Path(scene["review_html"])
        analysis = Path(scene["vision_analysis_json"])
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
