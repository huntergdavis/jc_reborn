#!/usr/bin/env python3
"""Build a current rollout manifest from scene-scoped restore specs."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


DEFAULT_SUMMARY = Path("docs/ps1/research/generated/restore_scene_specs_full_2026-03-19/summary.json")
DEFAULT_STATUS_JSON = Path("docs/ps1/research/CURRENT_STATUS_2026-03-21.json")
DEFAULT_JSON_OUTPUT = Path("docs/ps1/research/generated/restore_rollout_manifest_2026-03-21.json")
DEFAULT_MD_OUTPUT = Path("docs/ps1/research/generated/restore_rollout_manifest_2026-03-21.md")


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def build_status_maps(status_snapshot: dict) -> tuple[set[tuple[str, int]], set[tuple[str, int]], set[tuple[str, int | None]]]:
    verified = set()
    bringup = set()
    blocked = set()

    for family in status_snapshot.get("current_verified", []):
        ads_name = family["ads_name"]
        for ads_tag in family.get("ads_tags", []):
            verified.add((ads_name, ads_tag))

    for family in status_snapshot.get("current_bringup", []):
        ads_name = family["ads_name"]
        for ads_tag in family.get("ads_tags", []):
            bringup.add((ads_name, ads_tag))

    for row in status_snapshot.get("current_blocked_families", []):
        blocked.add((row["ads_name"], row.get("ads_tag")))

    return verified, bringup, blocked


def classify(
    row: dict,
    verified: set[tuple[str, int]],
    bringup: set[tuple[str, int]],
    blocked: set[tuple[str, int | None]],
) -> tuple[str, str]:
    key = (row["ads_name"], row["ads_tag"])
    if key in verified:
        return ("verified_live", "Validated in bounded DuckStation harness and counted as verified.")
    if key in bringup:
        return ("live_bringup", "Live in the generated header, but not yet counted as verified.")
    if key in blocked or (row["ads_name"], None) in blocked:
        return (
            "blocked_entry_path_or_unreliable_route",
            "Offline contract exists, but current entry path or route is not yet trustworthy enough for promotion.",
        )
    return (
        "artifact_ready_unverified",
        "Offline contract is generated and ready for grouped runtime promotion once route validation is available.",
    )


def build_manifest(summary: dict, status_snapshot: dict, status_snapshot_path: Path) -> dict:
    verified, bringup, blocked = build_status_maps(status_snapshot)
    rows = []
    buckets = {
        "verified_live": [],
        "live_bringup": [],
        "artifact_ready_unverified": [],
        "blocked_entry_path_or_unreliable_route": [],
    }

    for pilot in summary["pilots"]:
        status, reason = classify(pilot, verified, bringup, blocked)
        row = dict(pilot)
        row["status"] = status
        row["status_reason"] = reason
        rows.append(row)
        buckets[status].append(
            {
                "ads_name": row["ads_name"],
                "ads_tag": row["ads_tag"],
                "scene_index": row["scene_index"],
                "json_path": row["json_path"],
            }
        )

    return {
        "schema_version": 2,
        "artifact_kind": "restore_rollout_manifest",
        "scene_count": len(rows),
        "counts": {key: len(value) for key, value in buckets.items()},
        "live_ads_names": sorted(
            {
                row["ads_name"]
                for row in rows
                if row["status"] in {"verified_live", "live_bringup"}
            }
        ),
        "rows": rows,
        "buckets": buckets,
        "status_snapshot_path": str(status_snapshot_path),
    }


def write_markdown(path: Path, manifest: dict) -> None:
    lines = [
        "# Restore Rollout Manifest",
        "",
        "Date: 2026-03-21",
        "",
        f"- scenes: `{manifest['scene_count']}`",
        f"- verified live: `{manifest['counts']['verified_live']}`",
        f"- live bring-up: `{manifest['counts']['live_bringup']}`",
        f"- artifact-ready unverified: `{manifest['counts']['artifact_ready_unverified']}`",
        f"- blocked/unreliable route: `{manifest['counts']['blocked_entry_path_or_unreliable_route']}`",
        "",
        "## Verified Live",
        "",
    ]
    for row in manifest["buckets"]["verified_live"]:
        lines.append(f"- `{row['ads_name']} tag {row['ads_tag']}` scene `{row['scene_index']}`")

    lines.extend(["", "## Live Bring-Up", ""])
    for row in manifest["buckets"]["live_bringup"]:
        lines.append(f"- `{row['ads_name']} tag {row['ads_tag']}` scene `{row['scene_index']}`")

    lines.extend(["", "## Artifact-Ready Unverified", ""])
    for row in manifest["buckets"]["artifact_ready_unverified"]:
        lines.append(f"- `{row['ads_name']} tag {row['ads_tag']}` scene `{row['scene_index']}`")

    lines.extend(["", "## Blocked / Unreliable Route", ""])
    for row in manifest["buckets"]["blocked_entry_path_or_unreliable_route"]:
        lines.append(f"- `{row['ads_name']} tag {row['ads_tag']}` scene `{row['scene_index']}`")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--summary", type=Path, default=DEFAULT_SUMMARY)
    ap.add_argument("--status-json", type=Path, default=DEFAULT_STATUS_JSON)
    ap.add_argument("--json-output", type=Path, default=DEFAULT_JSON_OUTPUT)
    ap.add_argument("--md-output", type=Path, default=DEFAULT_MD_OUTPUT)
    return ap


def main() -> int:
    args = build_parser().parse_args()
    summary = load_json(args.summary)
    status_snapshot = load_json(args.status_json)
    manifest = build_manifest(summary, status_snapshot, args.status_json)
    write_json(args.json_output, manifest)
    write_markdown(args.md_output, manifest)
    print(
        json.dumps(
            {
                "json_output": str(args.json_output),
                "md_output": str(args.md_output),
                "counts": manifest["counts"],
                "live_ads_names": manifest["live_ads_names"],
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
