#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
from typing import Any


def html_escape(text: Any) -> str:
    return (
        str(text)
        .replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def parse_raw_screen_type(raw_response: str) -> str:
    try:
        payload = json.loads(raw_response)
    except Exception:
        return "unknown"
    return str(payload.get("screen_type", "unknown"))


def resolve_summary_path(path_value: str | None, summary_path: Path) -> Path | None:
    if not path_value:
        return None
    path = Path(path_value)
    if path.is_absolute():
        return path if path.exists() else None
    candidate = (summary_path.parent / path).resolve()
    if candidate.exists():
        return candidate
    return None


def load_rows(summary_path: Path) -> list[dict[str, Any]]:
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    outdir = summary_path.parent
    rows: list[dict[str, Any]] = []
    for item in summary.get("frames", []):
        analysis_path = outdir / item["analysis_path"]
        analysis = json.loads(analysis_path.read_text(encoding="utf-8"))
        scene_id = item["scene_id"]
        frame = item["frame"]
        image_path = resolve_summary_path(item.get("relative_frame_path"), summary_path)
        if image_path is None:
            image_path = summary_path.parent / "regtest-references" / scene_id / "frames" / frame
        meta = analysis.get("_meta", {})
        heuristic = meta.get("heuristic_state", {})
        rows.append(
            {
                "scene_id": scene_id,
                "frame": frame,
                "image_path": image_path,
                "analysis_path": analysis_path,
                "final_screen_type": analysis.get("screen_type", "unknown"),
                "final_sprites_visible": analysis.get("sprites_visible", False),
                "final_confidence": analysis.get("confidence", 0.0),
                "final_reason": analysis.get("reason", ""),
                "raw_screen_type": parse_raw_screen_type(meta.get("raw_response", "")),
                "heuristic_screen_type": heuristic.get("screen_type", "unknown"),
                "heuristic_raw_screen_type": heuristic.get("raw_screen_type", "unknown"),
                "heuristic_sprites_visible": heuristic.get("sprites_visible", False),
                "heuristic_action_summary": heuristic.get("action_summary", ""),
                "elapsed_wall_sec": meta.get("elapsed_wall_sec", item.get("elapsed_wall_sec")),
            }
        )
    return rows


def build_html(title: str, rows: list[dict[str, Any]], out_html: Path) -> str:
    body_rows: list[str] = []
    for row in rows:
        rel_image = os.path.relpath(row["image_path"], out_html.parent)
        analysis_name = row["analysis_path"].name
        mismatch = row["raw_screen_type"] != row["heuristic_screen_type"]
        row_class = "mismatch" if mismatch else "agree"
        body_rows.append(
            f"""
            <tr class="{row_class}">
              <td class="image-cell">
                <img src="{html_escape(rel_image)}" alt="{html_escape(row['frame'])}">
                <div class="meta">{html_escape(row['scene_id'])} / {html_escape(row['frame'])}</div>
                <div class="meta">{html_escape(analysis_name)}</div>
              </td>
              <td>
                <div><strong>{html_escape(row['final_screen_type'])}</strong></div>
                <div>sprites_visible: {html_escape(row['final_sprites_visible'])}</div>
                <div>confidence: {html_escape(row['final_confidence'])}</div>
                <div class="reason">{html_escape(row['final_reason'])}</div>
              </td>
              <td>
                <div><strong>{html_escape(row['raw_screen_type'])}</strong></div>
                <div class="reason">{html_escape(row['final_reason'])}</div>
              </td>
              <td>
                <div><strong>{html_escape(row['heuristic_screen_type'])}</strong></div>
                <div>raw: {html_escape(row['heuristic_raw_screen_type'])}</div>
                <div>sprites_visible: {html_escape(row['heuristic_sprites_visible'])}</div>
                <div class="reason">{html_escape(row['heuristic_action_summary'])}</div>
              </td>
              <td>{html_escape(row['elapsed_wall_sec'])}</td>
            </tr>
            """
        )

    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>{html_escape(title)}</title>
  <style>
    :root {{
      --bg: #f6f1e8;
      --fg: #1f1b16;
      --muted: #6a6157;
      --line: #cdbfae;
      --agree: #edf4ea;
      --mismatch: #fff1de;
      --panel: #fffaf3;
    }}
    body {{
      margin: 24px;
      font-family: Georgia, "Iowan Old Style", serif;
      background: linear-gradient(180deg, #f8f3ea 0%, #efe5d3 100%);
      color: var(--fg);
    }}
    h1 {{
      margin: 0 0 8px;
      font-size: 32px;
    }}
    p {{
      margin: 0 0 18px;
      color: var(--muted);
      max-width: 900px;
    }}
    table {{
      width: 100%;
      border-collapse: collapse;
      background: var(--panel);
      box-shadow: 0 8px 30px rgba(0,0,0,0.08);
    }}
    th, td {{
      border: 1px solid var(--line);
      padding: 12px;
      vertical-align: top;
      text-align: left;
    }}
    th {{
      background: #efe1cd;
      position: sticky;
      top: 0;
    }}
    tr.agree {{
      background: var(--agree);
    }}
    tr.mismatch {{
      background: var(--mismatch);
    }}
    img {{
      width: 320px;
      max-width: 100%;
      image-rendering: pixelated;
      border: 1px solid #9f907d;
      background: #000;
    }}
    .image-cell {{
      width: 340px;
    }}
    .meta {{
      margin-top: 6px;
      color: var(--muted);
      font-size: 13px;
    }}
    .reason {{
      margin-top: 8px;
      white-space: pre-wrap;
      line-height: 1.35;
    }}
  </style>
</head>
<body>
  <h1>{html_escape(title)}</h1>
  <p>Final = saved guarded result. Raw model = raw VLM screen type before override. Heuristic = lightweight classifier used for guardrails. Highlighted rows indicate raw model and heuristic disagree.</p>
  <table>
    <thead>
      <tr>
        <th>Frame</th>
        <th>Final Guarded Result</th>
        <th>Raw Model</th>
        <th>Heuristic</th>
        <th>Wall Sec</th>
      </tr>
    </thead>
    <tbody>
      {''.join(body_rows)}
    </tbody>
  </table>
</body>
</html>
"""


def main() -> None:
    parser = argparse.ArgumentParser(description="Render an HTML review comparing VLM output and heuristic state.")
    parser.add_argument("--summary-json", type=Path, required=True)
    parser.add_argument("--out-html", type=Path, required=True)
    parser.add_argument("--title", type=str, default="VLM vs Heuristic Review")
    args = parser.parse_args()

    rows = load_rows(args.summary_json)
    html = build_html(args.title, rows, args.out_html)
    args.out_html.parent.mkdir(parents=True, exist_ok=True)
    args.out_html.write_text(html, encoding="utf-8")


if __name__ == "__main__":
    main()
