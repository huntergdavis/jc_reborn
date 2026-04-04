#!/usr/bin/env python3
from __future__ import annotations

import argparse
import html
import json
import os
from pathlib import Path


def fmt(value):
    if value is None or value == "":
        return "n/a"
    if isinstance(value, bool):
        return "true" if value else "false"
    return str(value)


def rel(src: Path, dst_dir: Path) -> str:
    return os.path.relpath(src.resolve(), dst_dir.resolve())


def resolve_capture_path(path_value: str, base_dir: Path) -> Path:
    path = Path(path_value)
    if path.is_absolute():
        return path.resolve()
    return (base_dir / path).resolve()


def load_frames(result: dict, result_path: Path) -> list[Path]:
    frames_dir = result.get("paths", {}).get("frames_dir")
    if not frames_dir:
        return []
    path = resolve_capture_path(frames_dir, result_path.parent)
    if not path.is_dir():
        return []
    return sorted(path.glob("frame_*.png"))


def frame_meta_by_name(result: dict) -> dict[str, dict]:
    timeline = result.get("outcome", {}).get("visual_timeline", []) or []
    return {entry.get("frame", ""): entry for entry in timeline if entry.get("frame")}


def render_frames(frames: list[Path], meta: dict[str, dict], output_path: Path) -> str:
    if not frames:
        return """
<section class="card">
  <h2>Frames</h2>
  <div class="meta">No frames found.</div>
</section>
"""

    items = []
    for frame in frames:
        info = meta.get(frame.name, {})
        items.append(
            f"""
<figure class="frame-card">
  <figcaption>
    <code>{html.escape(frame.name)}</code>
    <span>{html.escape(fmt(info.get('screen_type')))}</span>
    <span>{html.escape(fmt(info.get('scene_family')))}</span>
  </figcaption>
  <img loading="lazy" src="{html.escape(rel(frame, output_path.parent))}" alt="{html.escape(frame.name)}">
  <div class="path">{html.escape(str(frame.resolve()))}</div>
</figure>
"""
        )
    return f"""
<section class="card">
  <h2>Raw Timeline</h2>
  <div class="meta">{len(frames)} captured frames in chronological order.</div>
  <div class="frame-grid">
    {''.join(items)}
  </div>
</section>
"""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--result-json", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--title", default="Regtest Run Review")
    args = ap.parse_args()

    result_path = Path(args.result_json).resolve()
    output_path = Path(args.output).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    data = json.loads(result_path.read_text())
    outcome = data.get("outcome", {})
    scene = data.get("scene", {})
    config = data.get("config", {})
    paths = data.get("paths", {})
    frames = load_frames(data, result_path)
    frame_meta = frame_meta_by_name(data)

    summary_boxes = [
        ("Scene", f"{scene.get('ads_name')} {scene.get('tag')}"),
        ("Boot", scene.get("boot_string")),
        ("State Hash", outcome.get("state_hash")),
        ("Frames", outcome.get("frames_captured")),
        ("Exit Code", outcome.get("exit_code")),
        ("Timed Out", outcome.get("timed_out")),
        ("Launched", outcome.get("scene_markers_last", {}).get("launched")),
        ("Visual Broken", outcome.get("likely_visual_broken")),
        ("BMP OK", outcome.get("scene_markers_last", {}).get("bmp_ok")),
        ("BMP Fail", outcome.get("scene_markers_last", {}).get("bmp_fail")),
        ("Sprites", outcome.get("scene_markers_last", {}).get("sprite_count_estimate")),
        ("CPU", config.get("cpu_mode")),
    ]

    summary_html = "".join(
        f'<div class="box"><div class="k">{html.escape(str(k))}</div><div class="v">{html.escape(fmt(v))}</div></div>'
        for k, v in summary_boxes
    )

    path_rows = "".join(
        f"<tr><th>{html.escape(str(k))}</th><td>{html.escape(str(v))}</td></tr>"
        for k, v in sorted(paths.items())
    )

    doc = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>{html.escape(args.title)}</title>
  <style>
    :root {{
      --bg:#0f1419; --panel:#182028; --panel2:#0d1117; --text:#e6edf3;
      --muted:#98a6b3; --border:#2f3b46; --accent:#f2cc60;
    }}
    html,body {{ margin:0; background:var(--bg); color:var(--text); font:14px/1.45 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace; }}
    main {{ max-width:1600px; margin:0 auto; padding:24px; }}
    h1,h2 {{ margin:0; }}
    .meta {{ color:var(--muted); margin-top:8px; }}
    .summary-grid {{ display:grid; grid-template-columns:repeat(4,minmax(0,1fr)); gap:10px; margin:18px 0 24px; }}
    .box,.card {{ background:var(--panel); border:1px solid var(--border); border-radius:12px; }}
    .box {{ padding:12px; }}
    .box .k {{ color:var(--muted); font-size:12px; margin-bottom:6px; }}
    .box .v {{ word-break:break-all; }}
    .card {{ padding:18px; margin-bottom:18px; }}
    .frame-grid {{ display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:16px; margin-top:12px; }}
    .frame-card {{ margin:0; background:var(--panel2); border:1px solid var(--border); border-radius:10px; overflow:hidden; }}
    .frame-card figcaption {{ display:flex; gap:10px; flex-wrap:wrap; padding:10px 12px; border-bottom:1px solid var(--border); color:var(--accent); }}
    .frame-card img {{ display:block; width:100%; height:auto; image-rendering:pixelated; background:#000; }}
    .path {{ padding:10px 12px; color:var(--muted); font-size:12px; word-break:break-all; }}
    table {{ width:100%; border-collapse:collapse; margin-top:12px; }}
    th,td {{ text-align:left; vertical-align:top; padding:8px 10px; border-top:1px solid var(--border); }}
    th {{ width:180px; color:var(--muted); }}
    @media (max-width:1000px) {{
      .summary-grid,.frame-grid {{ grid-template-columns:1fr; }}
    }}
  </style>
</head>
<body>
  <main>
    <h1>{html.escape(args.title)}</h1>
    <div class="meta">Standalone regtest review page for a single PS1 run.</div>

    <div class="summary-grid">
      {summary_html}
    </div>

    <section class="card">
      <h2>Paths</h2>
      <table>
        {path_rows}
      </table>
    </section>

    {render_frames(frames, frame_meta, output_path)}
  </main>
</body>
</html>
"""
    output_path.write_text(doc, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
