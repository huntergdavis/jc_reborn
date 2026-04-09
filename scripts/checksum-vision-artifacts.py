#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import argparse
import json
from pathlib import Path


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        while True:
            chunk = fh.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def main() -> None:
    project_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Write checksums for a published vision pipeline bundle.")
    parser.add_argument("--root", type=Path, default=project_root / "vision-artifacts" / "vision-reference-pipeline-current")
    args = parser.parse_args()

    root = args.root.resolve()
    targets = [
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
    ]
    rows = []
    for path in targets:
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
