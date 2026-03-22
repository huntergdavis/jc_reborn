#!/usr/bin/env python3
"""Group scene-scoped restore specs by shared restore contract."""

from __future__ import annotations

import argparse
import glob
import hashlib
import json
from pathlib import Path


DEFAULT_SPEC_DIR = Path("docs/ps1/research/generated/restore_scene_specs_full_2026-03-19")
DEFAULT_ROLLOUT = Path("docs/ps1/research/generated/restore_rollout_manifest_2026-03-21.json")
DEFAULT_JSON_OUTPUT = Path("docs/ps1/research/generated/restore_scene_clusters_2026-03-21.json")
DEFAULT_MD_OUTPUT = Path("docs/ps1/research/generated/restore_scene_clusters_2026-03-21.md")


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def contract_signature(spec: dict) -> dict:
    return {
        "ttm_names": spec["restore_template"]["ttm_names"],
        "unique_rects": spec["restore_template"]["unique_rects"],
        "bmps": spec["scene_resources"]["bmps"],
        "scrs": spec["scene_resources"]["scrs"],
        "ttms": spec["scene_resources"]["ttms"],
    }


def signature_id(signature: dict) -> str:
    payload = json.dumps(signature, sort_keys=True, separators=(",", ":"))
    return hashlib.sha1(payload.encode("utf-8")).hexdigest()[:12]


def load_rollout_map(path: Path) -> dict[tuple[str, int], dict]:
    rollout = load_json(path)
    rows = {}
    for row in rollout["rows"]:
        rows[(row["ads_name"], row["ads_tag"])] = row
    return rows


def build_clusters(spec_dir: Path, rollout_map: dict[tuple[str, int], dict]) -> dict:
    clusters: dict[tuple[str, str], dict] = {}

    for raw_path in sorted(glob.glob(str(spec_dir / "*.json"))):
        path = Path(raw_path)
        if path.name == "summary.json":
            continue

        spec = load_json(path)
        selected = spec["selected_scene"]
        signature = contract_signature(spec)
        sig_id = signature_id(signature)
        cluster_key = (selected["ads_name"], sig_id)
        rollout_row = rollout_map[(selected["ads_name"], selected["ads_tag"])]

        cluster = clusters.setdefault(
            cluster_key,
            {
                "cluster_id": f"{selected['ads_name'].lower().replace('.', '-')}:{sig_id}",
                "ads_name": selected["ads_name"],
                "signature_id": sig_id,
                "restore_score_max": selected["restore_score"],
                "restore_score_min": selected["restore_score"],
                "scene_count": 0,
                "statuses": {
                    "verified_live": 0,
                    "live_bringup": 0,
                    "artifact_ready_unverified": 0,
                    "blocked_entry_path_or_unreliable_route": 0,
                },
                "signature": signature,
                "scene_tags": [],
                "scene_indices": [],
                "spec_paths": [],
            },
        )

        cluster["restore_score_max"] = max(cluster["restore_score_max"], selected["restore_score"])
        cluster["restore_score_min"] = min(cluster["restore_score_min"], selected["restore_score"])
        cluster["scene_count"] += 1
        cluster["statuses"][rollout_row["status"]] += 1
        cluster["scene_tags"].append(selected["ads_tag"])
        cluster["scene_indices"].append(selected["scene_index"])
        cluster["spec_paths"].append(str(path))

    rows = []
    for cluster in clusters.values():
        scene_tags = sorted(cluster["scene_tags"])
        scene_indices = sorted(cluster["scene_indices"])
        statuses = cluster["statuses"]
        live_count = statuses["verified_live"] + statuses["live_bringup"]
        if statuses["blocked_entry_path_or_unreliable_route"] > 0:
            promotion = "blocked_entry_path_or_unreliable_route"
        elif statuses["verified_live"] == cluster["scene_count"]:
            promotion = "verified_live"
        elif live_count == cluster["scene_count"] and statuses["live_bringup"] > 0:
            promotion = "live_bringup"
        elif live_count > 0:
            promotion = "mixed_live_and_unverified"
        else:
            promotion = "artifact_ready_unverified"

        rows.append(
            {
                **cluster,
                "scene_tags": scene_tags,
                "scene_indices": scene_indices,
                "promotion_state": promotion,
                "preferred_spec_path": cluster["spec_paths"][0],
            }
        )

    rows.sort(key=lambda row: (-row["scene_count"], row["ads_name"], -row["restore_score_max"], row["scene_tags"][0]))
    return {
        "schema_version": 2,
        "artifact_kind": "restore_scene_cluster_report",
        "cluster_count": len(rows),
        "scene_count": sum(row["scene_count"] for row in rows),
        "clusters": rows,
    }


def write_markdown(path: Path, payload: dict) -> None:
    lines = [
        "# Restore Scene Clusters",
        "",
        "Date: 2026-03-21",
        "",
        f"- clusters: `{payload['cluster_count']}`",
        f"- scenes: `{payload['scene_count']}`",
        "",
        "## Largest clusters",
        "",
    ]

    for row in payload["clusters"][:20]:
        lines.append(
            f"- `{row['ads_name']}` tags `{row['scene_tags']}`"
            f" scenes `{row['scene_indices']}` count `{row['scene_count']}`"
            f" state `{row['promotion_state']}` score `{row['restore_score_min']}-{row['restore_score_max']}`"
        )

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--spec-dir", type=Path, default=DEFAULT_SPEC_DIR)
    ap.add_argument("--rollout", type=Path, default=DEFAULT_ROLLOUT)
    ap.add_argument("--json-output", type=Path, default=DEFAULT_JSON_OUTPUT)
    ap.add_argument("--md-output", type=Path, default=DEFAULT_MD_OUTPUT)
    return ap


def main() -> int:
    args = build_parser().parse_args()
    rollout_map = load_rollout_map(args.rollout)
    payload = build_clusters(args.spec_dir, rollout_map)
    write_json(args.json_output, payload)
    write_markdown(args.md_output, payload)
    print(
        json.dumps(
            {
                "json_output": str(args.json_output),
                "md_output": str(args.md_output),
                "cluster_count": payload["cluster_count"],
                "largest_cluster": payload["clusters"][0] if payload["clusters"] else None,
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
