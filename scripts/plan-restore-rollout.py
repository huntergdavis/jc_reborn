#!/usr/bin/env python3
"""Build a rollout manifest from scene-scoped restore specs."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


DEFAULT_SUMMARY = Path("docs/ps1/research/restore_scene_specs_full_2026-03-19/summary.json")
DEFAULT_JSON_OUTPUT = Path("docs/ps1/research/restore_rollout_manifest_2026-03-19.json")
DEFAULT_MD_OUTPUT = Path("docs/ps1/research/restore_rollout_manifest_2026-03-19.md")

LIVE_PROVEN = {
    ("STAND.ADS", 1),
    ("STAND.ADS", 2),
    ("STAND.ADS", 3),
    ("JOHNNY.ADS", 1),
    ("WALKSTUF.ADS", 2),
}

BLOCKED_ENTRY_PATH = {
    ("ACTIVITY.ADS", 4),
    ("FISHING.ADS", 1),
}


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def classify(row: dict) -> tuple[str, str]:
    key = (row["ads_name"], row["ads_tag"])
    if key in LIVE_PROVEN:
        return ("live_proven", "Validated in bounded DuckStation harness.")
    if key in BLOCKED_ENTRY_PATH:
        return ("blocked_entry_path", "Offline-ready spec exists, but current boot path does not reproduce a valid scene.")
    return ("offline_ready", "Spec is generated and ready for grouped runtime promotion once entry-path coverage exists.")


def build_manifest(summary: dict) -> dict:
    rows = []
    buckets = {
        "live_proven": [],
        "offline_ready": [],
        "blocked_entry_path": [],
    }

    for pilot in summary["pilots"]:
        status, reason = classify(pilot)
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
        "schema_version": 1,
        "artifact_kind": "restore_rollout_manifest",
        "scene_count": len(rows),
        "counts": {key: len(value) for key, value in buckets.items()},
        "live_ads_names": sorted({row["ads_name"] for row in rows if row["status"] == "live_proven"}),
        "rows": rows,
        "buckets": buckets,
    }


def write_markdown(path: Path, manifest: dict) -> None:
    lines = [
        "# Restore Rollout Manifest",
        "",
        "Date: 2026-03-19",
        "",
        f"- scenes: `{manifest['scene_count']}`",
        f"- live proven: `{manifest['counts']['live_proven']}`",
        f"- offline ready: `{manifest['counts']['offline_ready']}`",
        f"- blocked by entry path: `{manifest['counts']['blocked_entry_path']}`",
        "",
        "## Live proven",
        "",
    ]
    for row in manifest["buckets"]["live_proven"]:
        lines.append(f"- `{row['ads_name']} tag {row['ads_tag']}` scene `{row['scene_index']}`")

    lines.extend(["", "## Offline ready", ""])
    for row in manifest["buckets"]["offline_ready"]:
        lines.append(f"- `{row['ads_name']} tag {row['ads_tag']}` scene `{row['scene_index']}`")

    lines.extend(["", "## Blocked by entry path", ""])
    for row in manifest["buckets"]["blocked_entry_path"]:
        lines.append(f"- `{row['ads_name']} tag {row['ads_tag']}` scene `{row['scene_index']}`")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--summary", type=Path, default=DEFAULT_SUMMARY)
    ap.add_argument("--json-output", type=Path, default=DEFAULT_JSON_OUTPUT)
    ap.add_argument("--md-output", type=Path, default=DEFAULT_MD_OUTPUT)
    return ap


def main() -> int:
    args = build_parser().parse_args()
    summary = load_json(args.summary)
    manifest = build_manifest(summary)
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
