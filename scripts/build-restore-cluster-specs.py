#!/usr/bin/env python3
"""Build one restore spec per shared restore cluster."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


DEFAULT_CLUSTER_REPORT = Path("docs/ps1/research/generated/restore_scene_clusters_2026-03-21.json")
DEFAULT_OUTPUT_DIR = Path("docs/ps1/research/generated/restore_cluster_specs_2026-03-21")


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def write_markdown(path: Path, spec: dict) -> None:
    lines = [
        "# Restore Cluster Spec",
        "",
        "Date: 2026-03-21",
        "",
        f"- ads: `{spec['selected_scene']['ads_name']}`",
        f"- selected tag: `{spec['selected_scene']['ads_tag']}`",
        f"- cluster tags: `{spec['recommended_runtime_scope']['ads_tags']}`",
        f"- cluster scenes: `{spec['recommended_runtime_scope']['scene_indices']}`",
        f"- cluster status: `{spec['cluster']['promotion_state']}`",
        f"- cluster size: `{spec['cluster']['scene_count']}`",
        "",
        "## Notes",
        "",
    ]
    for note in spec["recommended_runtime_scope"]["notes"]:
        lines.append(f"- {note}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def slugify_cluster(cluster_id: str) -> str:
    return cluster_id.replace(":", "-")


def build_cluster_spec(cluster: dict) -> dict:
    base = load_json(Path(cluster["preferred_spec_path"]))
    base["artifact_kind"] = "restore_cluster_spec"
    base["cluster"] = {
        "cluster_id": cluster["cluster_id"],
        "promotion_state": cluster["promotion_state"],
        "scene_count": cluster["scene_count"],
        "statuses": cluster["statuses"],
        "signature_id": cluster["signature_id"],
    }
    base["recommended_runtime_scope"] = {
        **base["recommended_runtime_scope"],
        "ads_tags": cluster["scene_tags"],
        "scene_indices": cluster["scene_indices"],
        "notes": list(base["recommended_runtime_scope"]["notes"])
        + [
            "This spec was lifted from a shared restore-contract cluster.",
            "Promote the whole tag list together when runtime validation confirms the contract.",
        ],
    }
    return base


def build_summary(specs: list[dict], output_dir: Path) -> dict:
    rows = []
    for spec in specs:
        cluster = spec["cluster"]
        sel = spec["selected_scene"]
        slug = slugify_cluster(cluster["cluster_id"])
        rows.append(
            {
                "cluster_id": cluster["cluster_id"],
                "ads_name": sel["ads_name"],
                "selected_ads_tag": sel["ads_tag"],
                "scene_count": cluster["scene_count"],
                "promotion_state": cluster["promotion_state"],
                "scene_tags": spec["recommended_runtime_scope"]["ads_tags"],
                "scene_indices": spec["recommended_runtime_scope"]["scene_indices"],
                "json_path": str(output_dir / f"{slug}.json"),
                "md_path": str(output_dir / f"{slug}.md"),
            }
        )
    return {
        "schema_version": 2,
        "artifact_kind": "restore_cluster_spec_batch",
        "cluster_count": len(rows),
        "clusters": rows,
    }


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cluster-report", type=Path, default=DEFAULT_CLUSTER_REPORT)
    ap.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    return ap


def main() -> int:
    args = build_parser().parse_args()
    report = load_json(args.cluster_report)
    specs = []
    for cluster in report["clusters"]:
        spec = build_cluster_spec(cluster)
        slug = slugify_cluster(cluster["cluster_id"])
        write_json(args.output_dir / f"{slug}.json", spec)
        write_markdown(args.output_dir / f"{slug}.md", spec)
        specs.append(spec)

    summary = build_summary(specs, args.output_dir)
    write_json(args.output_dir / "summary.json", summary)
    print(
        json.dumps(
            {
                "output_dir": str(args.output_dir),
                "cluster_count": summary["cluster_count"],
                "first_cluster": summary["clusters"][0] if summary["clusters"] else None,
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
