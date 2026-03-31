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


def build_frame_link(row: dict, which: str, output_path: Path) -> str:
    entry = row.get(which) or {}
    image_path = entry.get("image_path")
    frame_name = entry.get("frame_name") or "missing"
    if not image_path:
        return esc(frame_name)
    path = Path(image_path)
    if not path.exists():
        return esc(frame_name)
    return f'<a href="{esc(rel(path, output_path.parent))}">{esc(frame_name)}</a>'


def build_html(report: dict, output_path: Path, title: str) -> str:
    rows_html = []
    for row in report.get("rows", []):
        status = row.get("status", "ok")
        row_class = "mismatch" if status == "mismatch" else "ok"
        base = row.get("base") or {}
        other = row.get("other") or {}
        rows_html.append(
            f"""
<tr class="{esc(row_class)}">
  <td>{esc(row.get('scene_label'))}</td>
  <td>{esc(row.get('frame_number'))}</td>
  <td>{build_frame_link(row, 'base', output_path)}</td>
  <td>{build_frame_link(row, 'other', output_path)}</td>
  <td>{esc(base.get('actor_summary', {}))}</td>
  <td>{esc(other.get('actor_summary', {}))}</td>
  <td>{esc(base.get('bmp_names', []))}</td>
  <td>{esc(other.get('bmp_names', []))}</td>
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
      --bg:#f6f1e8; --panel:#fffaf3; --line:#d6c7b5; --text:#241d17; --muted:#6f6457;
      --ok:#edf6ea; --mismatch:#fff0e1;
    }}
    body {{
      margin:24px;
      background:linear-gradient(180deg,#faf5ec 0%,#eee2d0 100%);
      color:var(--text);
      font:14px/1.45 ui-monospace,SFMono-Regular,Menlo,monospace;
    }}
    main {{ max-width: 1480px; margin: 0 auto; }}
    .meta {{ color: var(--muted); margin-bottom: 14px; }}
    table {{ width:100%; border-collapse:collapse; background:var(--panel); }}
    th,td {{ border:1px solid var(--line); text-align:left; padding:8px 10px; vertical-align:top; }}
    th {{ background:#efe1cd; }}
    tr.ok {{ background:var(--ok); }}
    tr.mismatch {{ background:var(--mismatch); }}
    a {{ color:#0f5ea8; text-decoration:none; }}
  </style>
</head>
<body>
  <main>
    <h1>{esc(title)}</h1>
    <div class="meta">mismatch_count={esc(report.get('mismatch_count', 0))}</div>
    <table>
      <thead>
        <tr>
          <th>Scene</th>
          <th>No</th>
          <th>Base Frame</th>
          <th>Other Frame</th>
          <th>Base Actors</th>
          <th>Other Actors</th>
          <th>Base BMPs</th>
          <th>Other BMPs</th>
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
    parser = argparse.ArgumentParser(description="Render host manifest reproducibility comparison as HTML")
    parser.add_argument("--report-json", type=Path, required=True)
    parser.add_argument("--out-html", type=Path, required=True)
    parser.add_argument("--title", default="Host Reproducibility Report")
    args = parser.parse_args()

    report = json.loads(args.report_json.read_text(encoding="utf-8"))
    args.out_html.parent.mkdir(parents=True, exist_ok=True)
    args.out_html.write_text(build_html(report, args.out_html, args.title), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
