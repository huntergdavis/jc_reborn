#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import argparse
import json
from pathlib import Path
from typing import Any


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        while True:
            chunk = fh.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def resolve_bundle_path(root: Path, path_value: str) -> Path:
    path = Path(path_value)
    if path.is_absolute():
        return path
    return (root / path).resolve()


def iter_path_values(payload: Any) -> list[str]:
    values: list[str] = []
    if isinstance(payload, dict):
        for value in payload.values():
            values.extend(iter_path_values(value))
    elif isinstance(payload, list):
        for value in payload:
            values.extend(iter_path_values(value))
    elif isinstance(payload, str):
        values.append(payload)
    return values


def main() -> None:
    project_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Write checksums for a published vision pipeline bundle.")
    parser.add_argument("--root", type=Path, default=project_root / "vision-artifacts" / "vision-reference-pipeline-current")
    args = parser.parse_args()

    root = args.root.resolve()
    targets = {
        root / "index.html",
        root / "pipeline-manifest.json",
        root / "artifact-catalog.json",
        root / "artifact-catalog.html",
        root / "scene-inventory.json",
        root / "scene-inventory.html",
        root / "scene-inventory.csv",
        root / "family-summary.csv",
        root / "strongest-scenes.json",
        root / "weakest-scenes.json",
        root / "top-confusion-pairs.json",
        root / "validation-report.json",
        root / "validation-report.html",
    }

    manifest_path = root / "pipeline-manifest.json"
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        for path_value in iter_path_values(manifest):
            resolved = resolve_bundle_path(root, path_value)
            if resolved.exists() and resolved.is_file():
                targets.add(resolved)

    catalog_path = root / "artifact-catalog.json"
    if catalog_path.exists():
        catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
        for path_value in iter_path_values(catalog):
            resolved = resolve_bundle_path(root, path_value)
            if resolved.exists() and resolved.is_file():
                targets.add(resolved)

    rows = []
    for path in sorted(targets):
        if path.exists():
            rows.append(
                {
                    "path": path.relative_to(root).as_posix(),
                    "sha256": sha256_file(path),
                    "size": path.stat().st_size,
                }
            )
    out = {"files": rows, "file_count": len(rows)}
    (root / "artifact-checksums.json").write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
