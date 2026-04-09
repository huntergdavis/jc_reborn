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


def resolve_report_path(path_value: str | None, report_path: Path) -> Path | None:
    if not path_value:
        return None
    path = Path(path_value)
    if path.is_absolute():
        return path if path.exists() else None
    candidate = (report_path.parent / path).resolve()
    if candidate.exists():
        return candidate
    return None


def build_html(report: dict, output_path: Path, title: str) -> str:
    rows_html = []
    for row in report.get("rows", []):
        status = row["status"]
        row_class = "mismatch" if status == "mismatch" else "ok"
        image_path = resolve_report_path(row.get("image_path"), output_path)
        if image_path is None:
            image_path = Path("host-script-review") / row["scene_label"].lower().replace(" ", "") / row["frame_name"]
            if not image_path.exists():
                image_path = None
        img_cell = (
            f'<a href="{esc(rel(image_path, output_path.parent))}">{esc(row["frame_name"])}</a>'
            if image_path else esc(row["frame_name"])
        )
        rows_html.append(
            f"""
<tr class="{esc(row_class)}">
  <td>{esc(row['scene_label'])}</td>
  <td>{img_cell}</td>
  <td>{esc(row['frame_number'])}</td>
  <td>{esc(', '.join(row['actual_entities']) or 'none')}</td>
  <td>{esc(', '.join(row['actual_bmps']) or 'none')}</td>
  <td>{esc(', '.join((row.get('expected') or {}).get('expect_entities', [])) or 'none')}</td>
  <td>{esc(', '.join(row['problems']) or 'none')}</td>
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
    main {{ max-width: 1280px; margin: 0 auto; }}
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
          <th>Frame</th>
          <th>No</th>
          <th>Actual Entities</th>
          <th>Actual BMPs</th>
          <th>Expected Entities</th>
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
    parser = argparse.ArgumentParser(description="Render host expectation comparison as HTML")
    parser.add_argument("--report-json", type=Path, required=True)
    parser.add_argument("--out-html", type=Path, required=True)
    parser.add_argument("--title", default="Host Script Expectation Report")
    args = parser.parse_args()

    report = json.loads(args.report_json.read_text(encoding="utf-8"))
    args.out_html.parent.mkdir(parents=True, exist_ok=True)
    args.out_html.write_text(build_html(report, args.out_html, args.title), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
