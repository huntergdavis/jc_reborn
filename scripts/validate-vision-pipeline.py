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
    strongest_scenes_path = root / "strongest-scenes.json"
    weakest_scenes_path = root / "weakest-scenes.json"
    top_confusion_pairs_path = root / "top-confusion-pairs.json"
    strongest_scenes = json.loads(strongest_scenes_path.read_text()) if strongest_scenes_path.exists() else {}
    weakest_scenes = json.loads(weakest_scenes_path.read_text()) if weakest_scenes_path.exists() else {}
    top_confusion_pairs = json.loads(top_confusion_pairs_path.read_text()) if top_confusion_pairs_path.exists() else {}
    required_summary_scene_keys = (
        "scene_id",
        "family",
        "frame_count",
        "review_html",
        "vision_analysis_json",
    )
    required_confusion_pair_keys = (
        "source_scene",
        "target_scene",
        "ratio",
        "family_source",
        "family_target",
        "source_review_html",
    )

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
    inventory_scene_map = {str(row["scene_id"]): row for row in inventory.get("scenes", [])}
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
    strongest_scene_ids = {
        str(row.get("scene_id"))
        for row in strongest_scenes.get("scenes", [])
        if isinstance(row, dict) and row.get("scene_id") is not None
    }
    weakest_scene_ids = {
        str(row.get("scene_id"))
        for row in weakest_scenes.get("scenes", [])
        if isinstance(row, dict) and row.get("scene_id") is not None
    }
    confusion_source_ids = {
        str(row.get("source_scene"))
        for row in top_confusion_pairs.get("pairs", [])
        if isinstance(row, dict) and row.get("source_scene") is not None
    }
    confusion_target_ids = {
        str(row.get("target_scene"))
        for row in top_confusion_pairs.get("pairs", [])
        if isinstance(row, dict) and row.get("target_scene") is not None
    }
    add_check(
        "strongest_scenes_exist_in_inventory",
        strongest_scenes_path.exists() and strongest_scene_ids <= inventory_scene_ids,
        (
            "strongest-scenes.json missing"
            if not strongest_scenes_path.exists() else
            f"unknown={sorted(strongest_scene_ids - inventory_scene_ids)[:10]}"
        ),
    )
    add_check(
        "weakest_scenes_exist_in_inventory",
        weakest_scenes_path.exists() and weakest_scene_ids <= inventory_scene_ids,
        (
            "weakest-scenes.json missing"
            if not weakest_scenes_path.exists() else
            f"unknown={sorted(weakest_scene_ids - inventory_scene_ids)[:10]}"
        ),
    )
    add_check(
        "top_confusion_pairs_exist_in_inventory",
        top_confusion_pairs_path.exists()
        and confusion_source_ids <= inventory_scene_ids
        and confusion_target_ids <= inventory_scene_ids,
        (
            "top-confusion-pairs.json missing"
            if not top_confusion_pairs_path.exists() else
            f"source_only={sorted(confusion_source_ids - inventory_scene_ids)[:10]}, "
            f"target_only={sorted(confusion_target_ids - inventory_scene_ids)[:10]}"
        ),
    )
    strongest_rows = strongest_scenes.get("scenes", [])
    weakest_rows = weakest_scenes.get("scenes", [])
    confusion_rows = top_confusion_pairs.get("pairs", [])
    strongest_sorted_expected = sorted(
        strongest_rows,
        key=lambda r: (-float(r.get("global_top1_ratio", float("-inf"))), str(r.get("scene_id", ""))),
    )
    weakest_sorted_expected = sorted(
        weakest_rows,
        key=lambda r: (float(r.get("global_top1_ratio", float("inf"))), str(r.get("scene_id", ""))),
    )
    confusion_sorted_expected = sorted(
        confusion_rows,
        key=lambda r: (-float(r.get("ratio", float("-inf"))), str(r.get("source_scene", "")), str(r.get("target_scene", ""))),
    )
    add_check(
        "strongest_scenes_sorted",
        strongest_scenes_path.exists() and strongest_rows == strongest_sorted_expected,
        "sorted descending by global_top1_ratio, scene_id",
    )
    add_check(
        "weakest_scenes_sorted",
        weakest_scenes_path.exists() and weakest_rows == weakest_sorted_expected,
        "sorted ascending by global_top1_ratio, scene_id",
    )
    add_check(
        "top_confusion_pairs_sorted",
        top_confusion_pairs_path.exists() and confusion_rows == confusion_sorted_expected,
        "sorted descending by ratio, source_scene, target_scene",
    )
    strongest_expected_rows = sorted(
        inventory.get("scenes", []),
        key=lambda r: (-float(r.get("global_top1_ratio", float("-inf"))), str(r.get("scene_id", ""))),
    )[:20]
    weakest_expected_rows = sorted(
        inventory.get("scenes", []),
        key=lambda r: (float(r.get("global_top1_ratio", float("inf"))), str(r.get("scene_id", ""))),
    )[:20]
    add_check(
        "strongest_scenes_match_inventory_selection",
        strongest_scenes_path.exists() and strongest_rows == strongest_expected_rows,
        f"rows={len(strongest_rows)}, expected={len(strongest_expected_rows)}",
    )
    add_check(
        "weakest_scenes_match_inventory_selection",
        weakest_scenes_path.exists() and weakest_rows == weakest_expected_rows,
        f"rows={len(weakest_rows)}, expected={len(weakest_expected_rows)}",
    )
    bad_confusion_families = []
    for row in confusion_rows:
        if not isinstance(row, dict):
            continue
        source_scene = row.get("source_scene")
        target_scene = row.get("target_scene")
        source_row = inventory_scene_map.get(str(source_scene))
        target_row = inventory_scene_map.get(str(target_scene))
        if source_row and row.get("family_source") != source_row.get("family"):
            bad_confusion_families.append(f"{source_scene}:source")
        if target_row and row.get("family_target") != target_row.get("family"):
            bad_confusion_families.append(f"{target_scene}:target")
    add_check(
        "top_confusion_pairs_families_match_inventory",
        top_confusion_pairs_path.exists() and not bad_confusion_families,
        (
            "top-confusion-pairs.json missing"
            if not top_confusion_pairs_path.exists() else
            ", ".join(bad_confusion_families[:10]) or "all present"
        ),
    )
    strongest_scene_id_list = [str(row.get("scene_id")) for row in strongest_rows if isinstance(row, dict)]
    weakest_scene_id_list = [str(row.get("scene_id")) for row in weakest_rows if isinstance(row, dict)]
    confusion_pair_list = [
        (str(row.get("source_scene")), str(row.get("target_scene")))
        for row in confusion_rows
        if isinstance(row, dict)
    ]
    add_check(
        "strongest_scenes_unique",
        strongest_scenes_path.exists() and len(strongest_scene_id_list) == len(set(strongest_scene_id_list)),
        f"rows={len(strongest_scene_id_list)}, unique={len(set(strongest_scene_id_list))}",
    )
    add_check(
        "weakest_scenes_unique",
        weakest_scenes_path.exists() and len(weakest_scene_id_list) == len(set(weakest_scene_id_list)),
        f"rows={len(weakest_scene_id_list)}, unique={len(set(weakest_scene_id_list))}",
    )
    add_check(
        "top_confusion_pairs_unique",
        top_confusion_pairs_path.exists() and len(confusion_pair_list) == len(set(confusion_pair_list)),
        f"rows={len(confusion_pair_list)}, unique={len(set(confusion_pair_list))}",
    )
    add_check(
        "strongest_scenes_count_capped",
        strongest_scenes_path.exists() and len(strongest_rows) <= 20,
        f"rows={len(strongest_rows)}, max=20",
    )
    add_check(
        "weakest_scenes_count_capped",
        weakest_scenes_path.exists() and len(weakest_rows) <= 20,
        f"rows={len(weakest_rows)}, max=20",
    )
    add_check(
        "top_confusion_pairs_count_capped",
        top_confusion_pairs_path.exists() and len(confusion_rows) <= 50,
        f"rows={len(confusion_rows)}, max=50",
    )
    expected_confusion_pair_map: dict[tuple[str, str], dict[str, object]] = {}
    for inventory_row in inventory.get("scenes", []):
        if not isinstance(inventory_row, dict):
            continue
        source_scene = str(inventory_row.get("scene_id", ""))
        source_family = inventory_row.get("family")
        source_review_html = inventory_row.get("review_html")
        for alt in inventory_row.get("top_alternates", []):
            if not isinstance(alt, dict) or alt.get("scene_id") is None:
                continue
            target_scene = str(alt["scene_id"])
            expected_confusion_pair_map[(source_scene, target_scene)] = {
                "source_scene": source_scene,
                "target_scene": target_scene,
                "ratio": alt.get("ratio"),
                "family_source": source_family,
                "family_target": target_scene.split("-", 1)[0],
                "source_review_html": source_review_html,
            }
    expected_confusion_rows = sorted(
        expected_confusion_pair_map.values(),
        key=lambda r: (-float(r.get("ratio", float("-inf"))), str(r.get("source_scene", "")), str(r.get("target_scene", ""))),
    )[:50]
    add_check(
        "top_confusion_pairs_match_inventory",
        top_confusion_pairs_path.exists() and confusion_rows == expected_confusion_rows,
        f"rows={len(confusion_rows)}, expected={len(expected_confusion_rows)}",
    )
    strongest_expected_count = min(20, inventory_scene_count)
    weakest_expected_count = min(20, inventory_scene_count)
    add_check(
        "strongest_scenes_count_matches_inventory_cap",
        strongest_scenes_path.exists() and len(strongest_rows) == strongest_expected_count,
        f"rows={len(strongest_rows)}, expected={strongest_expected_count}",
    )
    add_check(
        "weakest_scenes_count_matches_inventory_cap",
        weakest_scenes_path.exists() and len(weakest_rows) == weakest_expected_count,
        f"rows={len(weakest_rows)}, expected={weakest_expected_count}",
    )
    strongest_inventory_mismatches = []
    for row in strongest_rows:
        if not isinstance(row, dict):
            continue
        scene_id = str(row.get("scene_id", "<missing-scene-id>"))
        inventory_row = inventory_scene_map.get(scene_id)
        if inventory_row and any(
            row.get(key) != inventory_row.get(key)
            for key in ("family", "frame_count", "review_html", "vision_analysis_json")
        ):
            strongest_inventory_mismatches.append(scene_id)
    add_check(
        "strongest_scenes_match_inventory_rows",
        strongest_scenes_path.exists() and not strongest_inventory_mismatches,
        (
            "strongest-scenes.json missing"
            if not strongest_scenes_path.exists() else
            ", ".join(strongest_inventory_mismatches[:10]) or "all present"
        ),
    )
    strongest_full_row_mismatches = []
    for row in strongest_rows:
        if not isinstance(row, dict):
            continue
        scene_id = str(row.get("scene_id", "<missing-scene-id>"))
        inventory_row = inventory_scene_map.get(scene_id)
        if inventory_row and row != inventory_row:
            strongest_full_row_mismatches.append(scene_id)
    add_check(
        "strongest_scenes_fully_match_inventory_rows",
        strongest_scenes_path.exists() and not strongest_full_row_mismatches,
        (
            "strongest-scenes.json missing"
            if not strongest_scenes_path.exists() else
            ", ".join(strongest_full_row_mismatches[:10]) or "all present"
        ),
    )
    weakest_inventory_mismatches = []
    for row in weakest_rows:
        if not isinstance(row, dict):
            continue
        scene_id = str(row.get("scene_id", "<missing-scene-id>"))
        inventory_row = inventory_scene_map.get(scene_id)
        if inventory_row and any(
            row.get(key) != inventory_row.get(key)
            for key in ("family", "frame_count", "review_html", "vision_analysis_json")
        ):
            weakest_inventory_mismatches.append(scene_id)
    add_check(
        "weakest_scenes_match_inventory_rows",
        weakest_scenes_path.exists() and not weakest_inventory_mismatches,
        (
            "weakest-scenes.json missing"
            if not weakest_scenes_path.exists() else
            ", ".join(weakest_inventory_mismatches[:10]) or "all present"
        ),
    )
    weakest_full_row_mismatches = []
    for row in weakest_rows:
        if not isinstance(row, dict):
            continue
        scene_id = str(row.get("scene_id", "<missing-scene-id>"))
        inventory_row = inventory_scene_map.get(scene_id)
        if inventory_row and row != inventory_row:
            weakest_full_row_mismatches.append(scene_id)
    add_check(
        "weakest_scenes_fully_match_inventory_rows",
        weakest_scenes_path.exists() and not weakest_full_row_mismatches,
        (
            "weakest-scenes.json missing"
            if not weakest_scenes_path.exists() else
            ", ".join(weakest_full_row_mismatches[:10]) or "all present"
        ),
    )
    confusion_review_mismatches = []
    for row in confusion_rows:
        if not isinstance(row, dict):
            continue
        source_scene = str(row.get("source_scene", "<missing-source-scene>"))
        inventory_row = inventory_scene_map.get(source_scene)
        if inventory_row and row.get("source_review_html") != inventory_row.get("review_html"):
            confusion_review_mismatches.append(source_scene)
    add_check(
        "top_confusion_pairs_reviews_match_inventory",
        top_confusion_pairs_path.exists() and not confusion_review_mismatches,
        (
            "top-confusion-pairs.json missing"
            if not top_confusion_pairs_path.exists() else
            ", ".join(confusion_review_mismatches[:10]) or "all present"
        ),
    )
    malformed_strongest_scenes = []
    strongest_review_missing = []
    strongest_analysis_missing = []
    for row in strongest_scenes.get("scenes", []):
        scene_id = str(row.get("scene_id", "<missing-scene-id>"))
        if missing_keys(row, required_summary_scene_keys):
            malformed_strongest_scenes.append(scene_id)
            continue
        review_path = resolve_artifact_path(row["review_html"], strongest_scenes_path.parent)
        analysis_path = resolve_artifact_path(row["vision_analysis_json"], strongest_scenes_path.parent)
        if not review_path.exists():
            strongest_review_missing.append(scene_id)
        if not analysis_path.exists():
            strongest_analysis_missing.append(scene_id)
    add_check(
        "strongest_scenes_rows_complete",
        not malformed_strongest_scenes,
        ", ".join(malformed_strongest_scenes[:10]) or "all present",
    )
    add_check(
        "strongest_scenes_review_paths_exist",
        strongest_scenes_path.exists() and not strongest_review_missing,
        (
            "strongest-scenes.json missing"
            if not strongest_scenes_path.exists() else
            ", ".join(strongest_review_missing[:10]) or "all present"
        ),
    )
    add_check(
        "strongest_scenes_analysis_paths_exist",
        strongest_scenes_path.exists() and not strongest_analysis_missing,
        (
            "strongest-scenes.json missing"
            if not strongest_scenes_path.exists() else
            ", ".join(strongest_analysis_missing[:10]) or "all present"
        ),
    )
    malformed_weakest_scenes = []
    weakest_review_missing = []
    weakest_analysis_missing = []
    weakest_unknown_alternates = []
    for row in weakest_scenes.get("scenes", []):
        scene_id = str(row.get("scene_id", "<missing-scene-id>"))
        if missing_keys(row, required_summary_scene_keys):
            malformed_weakest_scenes.append(scene_id)
            continue
        review_path = resolve_artifact_path(row["review_html"], weakest_scenes_path.parent)
        analysis_path = resolve_artifact_path(row["vision_analysis_json"], weakest_scenes_path.parent)
        if not review_path.exists():
            weakest_review_missing.append(scene_id)
        if not analysis_path.exists():
            weakest_analysis_missing.append(scene_id)
        for alt in row.get("top_alternates", []):
            alt_scene_id = alt.get("scene_id") if isinstance(alt, dict) else None
            if alt_scene_id is not None and str(alt_scene_id) not in inventory_scene_ids:
                weakest_unknown_alternates.append(f"{scene_id}->{alt_scene_id}")
    add_check(
        "weakest_scenes_rows_complete",
        not malformed_weakest_scenes,
        ", ".join(malformed_weakest_scenes[:10]) or "all present",
    )
    add_check(
        "weakest_scenes_review_paths_exist",
        weakest_scenes_path.exists() and not weakest_review_missing,
        (
            "weakest-scenes.json missing"
            if not weakest_scenes_path.exists() else
            ", ".join(weakest_review_missing[:10]) or "all present"
        ),
    )
    add_check(
        "weakest_scenes_analysis_paths_exist",
        weakest_scenes_path.exists() and not weakest_analysis_missing,
        (
            "weakest-scenes.json missing"
            if not weakest_scenes_path.exists() else
            ", ".join(weakest_analysis_missing[:10]) or "all present"
        ),
    )
    add_check(
        "weakest_scene_alternates_exist_in_inventory",
        weakest_scenes_path.exists() and not weakest_unknown_alternates,
        (
            "weakest-scenes.json missing"
            if not weakest_scenes_path.exists() else
            ", ".join(weakest_unknown_alternates[:10]) or "all present"
        ),
    )
    malformed_confusion_pairs = []
    confusion_review_missing = []
    for row in top_confusion_pairs.get("pairs", []):
        source_scene = str(row.get("source_scene", "<missing-source-scene>"))
        if missing_keys(row, required_confusion_pair_keys):
            malformed_confusion_pairs.append(source_scene)
            continue
        review_path = resolve_artifact_path(row["source_review_html"], top_confusion_pairs_path.parent)
        if not review_path.exists():
            confusion_review_missing.append(source_scene)
    add_check(
        "top_confusion_pairs_rows_complete",
        not malformed_confusion_pairs,
        ", ".join(malformed_confusion_pairs[:10]) or "all present",
    )
    add_check(
        "top_confusion_pairs_review_paths_exist",
        top_confusion_pairs_path.exists() and not confusion_review_missing,
        (
            "top-confusion-pairs.json missing"
            if not top_confusion_pairs_path.exists() else
            ", ".join(confusion_review_missing[:10]) or "all present"
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
        "pipeline_index_html": "index.html",
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
    required_catalog_bank_keys = ("index_html", "index_json", "metadata_json", "features_npy")
    required_catalog_selfcheck_keys = (
        "index_html",
        "index_json",
        "quality_report_html",
        "quality_report_json",
        "confusion_report_html",
        "confusion_report_json",
        "family_report_html",
        "family_report_json",
    )
    required_manifest_bank_keys = ("index_html", "index_json", "metadata_json", "features_npy", "scene_count", "frame_count")
    required_manifest_selfcheck_keys = (
        "index_html",
        "index_json",
        "quality_html",
        "quality_json",
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
    bank_key_map = {
        "index_html": "index_html",
        "index_json": "index_json",
        "metadata_json": "metadata_json",
        "features_npy": "features_npy",
    }
    mismatched_catalog_manifest_bank = []
    for catalog_key, manifest_key in bank_key_map.items():
        if catalog_bank.get(catalog_key) != manifest_bank.get(manifest_key):
            mismatched_catalog_manifest_bank.append(catalog_key)
    add_check(
        "artifact_catalog_reference_bank_matches_manifest",
        not mismatched_catalog_manifest_bank,
        ", ".join(mismatched_catalog_manifest_bank[:10]) or "all present",
    )
    selfcheck_key_map = {
        "index_html": "index_html",
        "index_json": "index_json",
        "quality_report_html": "quality_html",
        "quality_report_json": "quality_json",
    }
    mismatched_catalog_manifest_selfcheck = []
    for catalog_key, manifest_key in selfcheck_key_map.items():
        if catalog_selfcheck.get(catalog_key) != manifest_selfcheck.get(manifest_key):
            mismatched_catalog_manifest_selfcheck.append(catalog_key)
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
