#!/usr/bin/env python3
from __future__ import annotations

import argparse
import html
import json
import os
from pathlib import Path


def esc(value: object) -> str:
    return html.escape(str(value))


def rel(path: Path, dst_dir: Path) -> str:
    return os.path.relpath(path.resolve(), dst_dir.resolve())


def resolve_root_path(path_value: str | None, report_path: Path) -> Path | None:
    if not path_value:
        return None
    path = Path(path_value)
    if path.is_absolute():
        return path.resolve()
    for base in [report_path.parent, *report_path.parents]:
        candidate = (base / path).resolve()
        if candidate.exists():
            return candidate
    return (report_path.parent / path).resolve()


def resolve_truth_frame_path(path_value: str | None, root: Path | None) -> Path | None:
    if not path_value:
        return None
    path = Path(path_value)
    if path.is_absolute():
        return path if path.exists() else None
    if root is None:
        return None
    candidate = (root / path).resolve()
    if candidate.exists():
        return candidate
    return None


def frame_image_path(actual_root: Path, scene_label: str, frame_name: str) -> Path | None:
    scene_dir_name = scene_label.lower().replace(" ", "")
    for suffix in (".bmp", ".png"):
        path = actual_root / scene_dir_name / "frames" / f"{frame_name}{suffix}"
        if path.is_file():
            return path
    return None


def format_character_diff(item: dict) -> str:
    delta = item.get("centroid_delta") or {}
    bbox_delta = item.get("bbox_delta") or {}
    problems = ", ".join(item.get("problems") or []) or "ok"
    return (
        f"{item.get('character')}: centroid "
        f"dx={delta.get('dx')} dy={delta.get('dy')} dist={delta.get('distance')} "
        f"bbox dw={bbox_delta.get('width')} dh={bbox_delta.get('height')} "
        f"[{problems}]"
    )


def build_html(report: dict, report_path: Path, output_path: Path, title: str) -> str:
    expected_root_value = report.get("expected_root")
    actual_root_value = report.get("actual_root")
    expected_root = resolve_root_path(expected_root_value, report_path)
    actual_root = resolve_root_path(actual_root_value, report_path)
    rows_html = []
    for row in report.get("rows", []):
        status = row.get("status", "unknown")
        row_class = "ok" if status == "ok" else "mismatch"
        frame_name = row.get("frame_name") or "n/a"
        image_path = None
        if row.get("frame_path"):
            image_path = resolve_truth_frame_path(row.get("frame_path"), actual_root or expected_root)
        if image_path is None and actual_root is not None and row.get("scene_label") and row.get("frame_name"):
            image_path = frame_image_path(actual_root, row["scene_label"], row["frame_name"])
        frame_cell = (
            f'<a href="{esc(rel(image_path, output_path.parent))}">{esc(frame_name)}</a>'
            if image_path
            else esc(frame_name)
        )
        rows_html.append(
            f"""
<tr class="{esc(row_class)}">
  <td>{esc(row.get('scene_label'))}</td>
  <td>{frame_cell}</td>
  <td>{esc(row.get('frame_number'))}</td>
  <td>{esc(', '.join(row.get('missing_characters', [])) or 'none')}</td>
  <td>{esc(', '.join(row.get('unexpected_characters', [])) or 'none')}</td>
  <td>{esc(' | '.join(format_character_diff(item) for item in row.get('shared_characters', [])) or 'none')}</td>
  <td>{esc(', '.join(row.get('problems', [])) or 'none')}</td>
</tr>
"""
        )

    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>{esc(title)}</title>
  <style>
    :root {{
      --bg: #f6f1e8;
      --panel: #fffaf3;
      --line: #d6c7b5;
      --text: #241d17;
      --muted: #6f6457;
      --ok: #edf6ea;
      --mismatch: #fff0e1;
    }}
    body {{
      margin: 24px;
      background: linear-gradient(180deg, #faf5ec 0%, #eee2d0 100%);
      color: var(--text);
      font: 14px/1.45 ui-monospace, SFMono-Regular, Menlo, monospace;
    }}
    main {{ max-width: 1400px; margin: 0 auto; }}
    .meta {{ color: var(--muted); margin-bottom: 14px; }}
    table {{ width: 100%; border-collapse: collapse; background: var(--panel); }}
    th, td {{ border: 1px solid var(--line); text-align: left; padding: 8px 10px; vertical-align: top; }}
    th {{ background: #efe1cd; }}
    tr.ok {{ background: var(--ok); }}
    tr.mismatch {{ background: var(--mismatch); }}
    a {{ color: #0f5ea8; text-decoration: none; }}
  </style>
</head>
<body>
  <main>
    <h1>{esc(title)}</h1>
    <div class="meta">expected_root={esc(expected_root or '.')} actual_root={esc(actual_root or '.')} mismatch_count={esc(report.get('mismatch_count', 0))} position_tolerance={esc(report.get('position_tolerance'))}</div>
    <table>
      <thead>
        <tr>
          <th>Scene</th>
          <th>Frame</th>
          <th>No</th>
          <th>Missing Characters</th>
          <th>Unexpected Characters</th>
          <th>Shared Character Drift</th>
          <th>Problems</th>
        </tr>
      </thead>
      <tbody>
        {''.join(rows_html)}
      </tbody>
    </table>
  </main>
</body>
</html>
"""


def main() -> int:
    parser = argparse.ArgumentParser(description="Render screenshot-level character truth comparison as HTML.")
    parser.add_argument("--report-json", type=Path, required=True)
    parser.add_argument("--out-html", type=Path, required=True)
    parser.add_argument("--title", default="Character Truth Comparison Report")
    args = parser.parse_args()

    report = json.loads(args.report_json.read_text(encoding="utf-8"))
    args.out_html.parent.mkdir(parents=True, exist_ok=True)
    args.out_html.write_text(build_html(report, args.report_json, args.out_html, args.title), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
