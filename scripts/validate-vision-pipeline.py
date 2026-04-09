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


def missing_keys(mapping: object, required_keys: tuple[str, ...]) -> list[str]:
    if not isinstance(mapping, dict):
        return list(required_keys)
    return [key for key in required_keys if key not in mapping]


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

    required_bank_scene_keys = ("scene_id", "family", "frame_count", "review_html")
    required_selfcheck_scene_keys = ("scene_id",)
    required_inventory_scene_keys = ("scene_id", "family", "frame_count", "review_html", "vision_analysis_json")
    required_catalog_scene_keys = ("scene_id", "family", "review_html", "vision_analysis_json")
    malformed_bank_scenes = []
    for row in bank_index.get("scenes", []):
        scene_id = str(row.get("scene_id", "<missing-scene-id>"))
        if missing_keys(row, required_bank_scene_keys):
            malformed_bank_scenes.append(scene_id)
    add_check(
        "bank_scene_rows_complete",
        not malformed_bank_scenes,
        ", ".join(malformed_bank_scenes[:10]) or "all present",
    )
    malformed_selfcheck_scenes = []
    for row in selfcheck_index.get("scenes", []):
        scene_id = str(row.get("scene_id", "<missing-scene-id>"))
        if missing_keys(row, required_selfcheck_scene_keys):
            malformed_selfcheck_scenes.append(scene_id)
    add_check(
        "selfcheck_scene_rows_complete",
        not malformed_selfcheck_scenes,
        ", ".join(malformed_selfcheck_scenes[:10]) or "all present",
    )

    bank_scene_count = len(bank_index["scenes"])
    selfcheck_scene_count = int(selfcheck_index["scene_count"])
    inventory_scene_count = int(inventory["scene_count"])
    catalog_declared_scene_count = int(
        (catalog or {}).get("scene_count", len((catalog or {}).get("scenes", [])))
    )
    add_check(
        "selfcheck_scene_count_matches_rows",
        selfcheck_scene_count == len(selfcheck_index.get("scenes", [])),
        f"declared={selfcheck_scene_count}, rows={len(selfcheck_index.get('scenes', []))}",
    )
    add_check(
        "scene_inventory_count_matches_rows",
        inventory_scene_count == len(inventory.get("scenes", [])),
        f"declared={inventory_scene_count}, rows={len(inventory.get('scenes', []))}",
    )
    add_check(
        "artifact_catalog_count_matches_rows",
        catalog is not None and catalog_declared_scene_count == len((catalog or {}).get("scenes", [])),
        (
            "artifact-catalog.json missing"
            if catalog is None else
            f"declared={catalog_declared_scene_count}, rows={len((catalog or {}).get('scenes', []))}"
        ),
    )
    bank_total_frame_count = int(bank_index.get("frame_count", -1))
    bank_row_frame_total = sum(
        int(row.get("frame_count", 0))
        for row in bank_index.get("scenes", [])
        if isinstance(row.get("frame_count"), int)
    )
    add_check(
        "bank_frame_count_matches_rows",
        bank_total_frame_count == bank_row_frame_total,
        f"declared={bank_total_frame_count}, rows={bank_row_frame_total}",
    )
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
    missing_catalog_top_level = []
    for name, path_value in (catalog or {}).get("top_level", {}).items():
        resolved = resolve_artifact_path(path_value, root)
        if not resolved.exists():
            missing_catalog_top_level.append(name)
    add_check(
        "artifact_catalog_top_level_exists",
        not missing_catalog_top_level,
        ", ".join(missing_catalog_top_level[:10]) or "all present",
    )
    top_level = (catalog or {}).get("top_level", {})
    mismatched_catalog_manifest_top_level = []
    expected_top_level_pairs = {
        "pipeline_manifest_json": "pipeline-manifest.json",
        "scene_inventory_json": manifest.get("inventory_json"),
        "scene_inventory_html": manifest.get("inventory_html"),
        "scene_inventory_csv": "scene-inventory.csv",
        "strongest_scenes_json": "strongest-scenes.json",
        "weakest_scenes_json": "weakest-scenes.json",
        "top_confusion_pairs_json": "top-confusion-pairs.json",
        "validation_report_json": "validation-report.json",
        "validation_report_html": "validation-report.html",
    }
    missing_catalog_manifest_top_level = []
    for catalog_key, expected_value in expected_top_level_pairs.items():
        if catalog_key not in top_level:
            missing_catalog_manifest_top_level.append(catalog_key)
        if top_level.get(catalog_key) != expected_value:
            mismatched_catalog_manifest_top_level.append(catalog_key)
    add_check(
        "artifact_catalog_top_level_complete",
        not missing_catalog_manifest_top_level,
        ", ".join(missing_catalog_manifest_top_level[:10]) or "all present",
    )
    add_check(
        "artifact_catalog_top_level_matches_manifest",
        not mismatched_catalog_manifest_top_level,
        ", ".join(mismatched_catalog_manifest_top_level[:10]) or "all present",
    )
    required_catalog_bank_keys = ("index_html", "metadata_json", "features_npy")
    required_catalog_selfcheck_keys = (
        "index_html",
        "quality_report_html",
        "confusion_report_html",
        "family_report_html",
    )
    required_manifest_bank_keys = ("index_html", "metadata_json", "features_npy", "scene_count", "frame_count")
    required_manifest_selfcheck_keys = (
        "index_html",
        "quality_report_html",
        "confusion_report_html",
        "family_report_html",
        "scene_count",
    )

    catalog_bank = (catalog or {}).get("reference_bank", {})
    catalog_selfcheck = (catalog or {}).get("reference_selfcheck", {})
    manifest_bank = manifest.get("reference_bank", {})
    manifest_selfcheck = manifest.get("reference_selfcheck", {})

    add_check(
        "artifact_catalog_reference_bank_complete",
        not missing_keys(catalog_bank, required_catalog_bank_keys),
        ", ".join(missing_keys(catalog_bank, required_catalog_bank_keys)[:10]) or "all present",
    )
    add_check(
        "artifact_catalog_reference_selfcheck_complete",
        not missing_keys(catalog_selfcheck, required_catalog_selfcheck_keys),
        ", ".join(missing_keys(catalog_selfcheck, required_catalog_selfcheck_keys)[:10]) or "all present",
    )
    add_check(
        "manifest_reference_bank_complete",
        not missing_keys(manifest_bank, required_manifest_bank_keys),
        ", ".join(missing_keys(manifest_bank, required_manifest_bank_keys)[:10]) or "all present",
    )
    add_check(
        "manifest_reference_selfcheck_complete",
        not missing_keys(manifest_selfcheck, required_manifest_selfcheck_keys),
        ", ".join(missing_keys(manifest_selfcheck, required_manifest_selfcheck_keys)[:10]) or "all present",
    )

    missing_catalog_bank = []
    for name, path_value in catalog_bank.items():
        if not isinstance(path_value, str):
            continue
        resolved = resolve_artifact_path(path_value, root)
        if not resolved.exists():
            missing_catalog_bank.append(name)
    add_check(
        "artifact_catalog_reference_bank_exists",
        not missing_catalog_bank,
        ", ".join(missing_catalog_bank[:10]) or "all present",
    )
    missing_catalog_selfcheck = []
    for name, path_value in catalog_selfcheck.items():
        if not isinstance(path_value, str):
            continue
        resolved = resolve_artifact_path(path_value, root)
        if not resolved.exists():
            missing_catalog_selfcheck.append(name)
    add_check(
        "artifact_catalog_reference_selfcheck_exists",
        not missing_catalog_selfcheck,
        ", ".join(missing_catalog_selfcheck[:10]) or "all present",
    )
    mismatched_catalog_manifest_bank = []
    for name, path_value in catalog_bank.items():
        if path_value != manifest_bank.get(name):
            mismatched_catalog_manifest_bank.append(name)
    add_check(
        "artifact_catalog_reference_bank_matches_manifest",
        not mismatched_catalog_manifest_bank,
        ", ".join(mismatched_catalog_manifest_bank[:10]) or "all present",
    )
    mismatched_catalog_manifest_selfcheck = []
    for name, path_value in catalog_selfcheck.items():
        if path_value != manifest_selfcheck.get(name):
            mismatched_catalog_manifest_selfcheck.append(name)
    add_check(
        "artifact_catalog_reference_selfcheck_matches_manifest",
        not mismatched_catalog_manifest_selfcheck,
        ", ".join(mismatched_catalog_manifest_selfcheck[:10]) or "all present",
    )
    missing_manifest_bank = []
    for name, path_value in manifest_bank.items():
        if not isinstance(path_value, str) or not path_value.endswith((".html", ".json", ".npy")):
            continue
        resolved = resolve_artifact_path(path_value, manifest_path.parent)
        if not resolved.exists():
            missing_manifest_bank.append(name)
    add_check(
        "manifest_reference_bank_exists",
        not missing_manifest_bank,
        ", ".join(missing_manifest_bank[:10]) or "all present",
    )
    missing_manifest_selfcheck = []
    for name, path_value in manifest_selfcheck.items():
        if not isinstance(path_value, str) or not path_value.endswith((".html", ".json", ".npy")):
            continue
        resolved = resolve_artifact_path(path_value, manifest_path.parent)
        if not resolved.exists():
            missing_manifest_selfcheck.append(name)
    add_check(
        "manifest_reference_selfcheck_exists",
        not missing_manifest_selfcheck,
        ", ".join(missing_manifest_selfcheck[:10]) or "all present",
    )
    add_check(
        "manifest_reference_bank_counts_match",
        manifest_bank.get("scene_count") == bank_scene_count
        and manifest_bank.get("frame_count") == bank_index.get("frame_count"),
        (
            f"manifest_scene_count={manifest_bank.get('scene_count')}, "
            f"index_scene_count={bank_scene_count}, "
            f"manifest_frame_count={manifest_bank.get('frame_count')}, "
            f"index_frame_count={bank_index.get('frame_count')}"
        ),
    )
    add_check(
        "manifest_reference_selfcheck_counts_match",
        manifest_selfcheck.get("scene_count") == selfcheck_scene_count,
        (
            f"manifest_scene_count={manifest_selfcheck.get('scene_count')}, "
            f"index_scene_count={selfcheck_scene_count}"
        ),
    )
    missing_manifest_top_level = []
    for name in ("inventory_json", "inventory_html", "artifact_catalog_json"):
        path_value = manifest.get(name)
        if not isinstance(path_value, str):
            missing_manifest_top_level.append(name)
            continue
        resolved = resolve_artifact_path(path_value, manifest_path.parent)
        if not resolved.exists():
            missing_manifest_top_level.append(name)
    add_check(
        "manifest_top_level_exists",
        not missing_manifest_top_level,
        ", ".join(missing_manifest_top_level[:10]) or "all present",
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
    malformed_inventory_scenes = []
    for scene in inventory["scenes"]:
        scene_id = str(scene.get("scene_id", "<missing-scene-id>"))
        review_value = scene.get("review_html")
        analysis_value = scene.get("vision_analysis_json")
        if missing_keys(scene, required_inventory_scene_keys):
            malformed_inventory_scenes.append(scene_id)
            continue
        review = resolve_artifact_path(review_value, inventory_path.parent)
        analysis = resolve_artifact_path(analysis_value, inventory_path.parent)
        if not review.exists():
            missing_reviews.append(scene_id)
        if not analysis.exists():
            missing_json.append(scene_id)
    add_check(
        "inventory_scene_rows_complete",
        not malformed_inventory_scenes,
        ", ".join(malformed_inventory_scenes[:10]) or "all present",
    )
    add_check("per_scene_reviews_exist", not missing_reviews, ", ".join(missing_reviews[:10]) or "all present")
    add_check("per_scene_analysis_exist", not missing_json, ", ".join(missing_json[:10]) or "all present")

    missing_catalog_reviews = []
    missing_catalog_json = []
    catalog_scene_map = {str(row["scene_id"]): row for row in (catalog or {}).get("scenes", [])}
    inventory_scene_map = {str(row["scene_id"]): row for row in inventory.get("scenes", [])}
    mismatched_catalog_paths = []
    malformed_catalog_scenes = []
    for scene_id, row in catalog_scene_map.items():
        review_value = row.get("review_html")
        analysis_value = row.get("vision_analysis_json")
        if missing_keys(row, required_catalog_scene_keys):
            malformed_catalog_scenes.append(scene_id)
            continue
        review = resolve_artifact_path(review_value, root)
        analysis = resolve_artifact_path(analysis_value, root)
        if not review.exists():
            missing_catalog_reviews.append(scene_id)
        if not analysis.exists():
            missing_catalog_json.append(scene_id)
        inventory_row = inventory_scene_map.get(scene_id)
        if inventory_row and (
            row.get("review_html") != inventory_row.get("review_html")
            or row.get("vision_analysis_json") != inventory_row.get("vision_analysis_json")
        ):
            mismatched_catalog_paths.append(scene_id)
    add_check(
        "artifact_catalog_scene_rows_complete",
        not malformed_catalog_scenes,
        ", ".join(malformed_catalog_scenes[:10]) or "all present",
    )
    add_check(
        "artifact_catalog_reviews_exist",
        not missing_catalog_reviews,
        ", ".join(missing_catalog_reviews[:10]) or "all present",
    )
    add_check(
        "artifact_catalog_analysis_exist",
        not missing_catalog_json,
        ", ".join(missing_catalog_json[:10]) or "all present",
    )
    add_check(
        "artifact_catalog_paths_match_inventory",
        not mismatched_catalog_paths,
        ", ".join(mismatched_catalog_paths[:10]) or "all present",
    )

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
