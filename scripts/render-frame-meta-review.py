#!/usr/bin/env python3
from __future__ import annotations

import argparse
import html
import importlib.util
import json
import os
from pathlib import Path


def load_summarize() -> object:
    script_path = Path(__file__).with_name("summarize-frame-meta.py")
    spec = importlib.util.spec_from_file_location("summarize_frame_meta", script_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load summarizer from {script_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.summarize


summarize = load_summarize()


ENTITY_COLORS = {
    "johnny": "#ff7a59",
    "mary": "#2bb673",
    "suzy": "#4f8cff",
    "unknown": "#f2cc60",
}


def esc(value: object) -> str:
    return html.escape(str(value))


def rel(path: Path, dst_dir: Path) -> str:
    return os.path.relpath(path.resolve(), dst_dir.resolve())


def resolve_capture_path(path_value: str, scene_dir: Path) -> Path:
    path = Path(path_value)
    if path.is_absolute():
        return path.resolve()
    return (scene_dir / path).resolve()


def box_style(draw: dict, image_size: tuple[int, int]) -> str:
    width, height = image_size
    left = max(0.0, min(100.0, (float(draw["x"]) / width) * 100.0))
    top = max(0.0, min(100.0, (float(draw["y"]) / height) * 100.0))
    w = max(0.5, min(100.0, (float(draw["width"]) / width) * 100.0))
    h = max(0.5, min(100.0, (float(draw["height"]) / height) * 100.0))
    color = ENTITY_COLORS.get(draw.get("entity"), ENTITY_COLORS["unknown"])
    return f"left:{left:.3f}%;top:{top:.3f}%;width:{w:.3f}%;height:{h:.3f}%;border-color:{color};"


def load_rows(meta_paths: list[Path], image_width: int, image_height: int) -> list[dict]:
    rows = []
    for meta_path in meta_paths:
        summary = summarize(meta_path)
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
        image_path = resolve_capture_path(meta["image_path"], meta_path.parent.parent)
        rows.append(
            {
                "meta_path": meta_path,
                "image_path": image_path,
                "summary": summary,
                "boxes": [
                    {
                        **draw,
                        "style": box_style(draw, (image_width, image_height)),
                    }
                    for draw in summary["actor_candidates"]
                ],
            }
        )
    return rows


def build_html(rows: list[dict], title: str, output_path: Path) -> str:
    cards: list[str] = []
    for row in rows:
        summary = row["summary"]
        image_rel = rel(row["image_path"], output_path.parent)
        labels = ", ".join(
            f"{esc(entity)}={count}"
            for entity, count in sorted(summary.get("actor_summary", {}).items())
        ) or "none"
        boxes = "".join(
            f'<div class="box" style="{esc(draw["style"])}"><span>{esc(draw["entity"])} {esc(draw["bmp_name"])} '
            f'#{esc(draw["sprite_no"])} @ ({esc(draw["x"])},{esc(draw["y"])})</span></div>'
            for draw in row["boxes"]
        )
        actor_rows = "".join(
            f"<tr><td>{esc(draw['entity'])}</td><td>{esc(draw['bmp_name'])}</td><td>{esc(draw['sprite_no'])}</td>"
            f"<td>{esc(draw['x'])},{esc(draw['y'])}</td><td>{esc(draw['width'])}x{esc(draw['height'])}</td><td>{esc(draw['occurrences'])}</td></tr>"
            for draw in summary["actor_candidates"]
        ) or '<tr><td colspan="6">No actor candidates</td></tr>'
        cards.append(
            f"""
<section class="card">
  <div class="card-head">
    <div>
      <h2>{esc(summary['scene_label'])} / frame {esc(summary['frame_number'])}</h2>
      <div class="meta">{esc(row['image_path'].name)}</div>
      <div class="meta">{esc(labels)}</div>
    </div>
    <div class="stats">
      <div><strong>draws</strong> {esc(summary['draw_count'])}</div>
      <div><strong>visible</strong> {esc(summary['visible_draw_count'])}</div>
      <div><strong>unique</strong> {esc(summary['visible_unique_draw_count'])}</div>
      <div><strong>actors</strong> {esc(summary['actor_candidate_draw_count'])}</div>
    </div>
  </div>
  <div class="frame-wrap">
    <div class="frame">
      <img src="{esc(image_rel)}" alt="{esc(row['image_path'].name)}">
      <div class="overlay">{boxes}</div>
    </div>
  </div>
  <table>
    <thead>
      <tr><th>Entity</th><th>BMP</th><th>Sprite</th><th>XY</th><th>Size</th><th>Occurrences</th></tr>
    </thead>
    <tbody>{actor_rows}</tbody>
  </table>
</section>
"""
        )
    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>{esc(title)}</title>
  <style>
    :root {{
      --bg: #f4efe6;
      --panel: #fffaf2;
      --line: #d4c5b4;
      --text: #231d18;
      --muted: #6d6156;
    }}
    body {{
      margin: 24px;
      background:
        radial-gradient(circle at top left, rgba(197, 173, 139, 0.18), transparent 32%),
        linear-gradient(180deg, #f8f4ec 0%, #ede3d2 100%);
      color: var(--text);
      font: 15px/1.45 Georgia, "Palatino Linotype", serif;
    }}
    main {{ max-width: 1280px; margin: 0 auto; }}
    h1 {{ margin: 0 0 8px; }}
    .intro {{ color: var(--muted); margin-bottom: 20px; }}
    .card {{
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 14px;
      box-shadow: 0 10px 24px rgba(0, 0, 0, 0.08);
      margin-bottom: 22px;
      padding: 18px;
    }}
    .card-head {{
      display: flex;
      justify-content: space-between;
      gap: 16px;
      align-items: flex-start;
      margin-bottom: 14px;
    }}
    .meta {{ color: var(--muted); }}
    .stats {{
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 8px 14px;
      font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
      font-size: 13px;
    }}
    .frame-wrap {{ overflow-x: auto; }}
    .frame {{
      position: relative;
      width: 640px;
      max-width: 100%;
      border: 1px solid var(--line);
      background: #000;
    }}
    .frame img {{
      display: block;
      width: 100%;
      height: auto;
      image-rendering: pixelated;
    }}
    .overlay {{
      position: absolute;
      inset: 0;
      pointer-events: none;
    }}
    .box {{
      position: absolute;
      border: 2px solid;
      box-sizing: border-box;
      background: rgba(255,255,255,0.05);
    }}
    .box span {{
      position: absolute;
      top: -1.5em;
      left: 0;
      white-space: nowrap;
      font: 12px/1.2 ui-monospace, SFMono-Regular, Menlo, monospace;
      background: rgba(255,250,242,0.92);
      color: #111;
      padding: 2px 4px;
      border: 1px solid currentColor;
    }}
    table {{
      width: 100%;
      border-collapse: collapse;
      margin-top: 14px;
      font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
      font-size: 13px;
    }}
    th, td {{
      border-top: 1px solid var(--line);
      padding: 8px 10px;
      text-align: left;
    }}
    th {{ color: var(--muted); }}
    @media (max-width: 900px) {{
      .card-head {{ display: block; }}
      .stats {{ margin-top: 10px; }}
      .box span {{ white-space: normal; max-width: 140px; }}
    }}
  </style>
</head>
<body>
  <main>
    <h1>{esc(title)}</h1>
    <div class="intro">Engine-truth frame review. Boxes come from deduped actor candidates derived from host capture metadata.</div>
    {''.join(cards)}
  </main>
</body>
</html>
"""


def main() -> int:
    parser = argparse.ArgumentParser(description="Render actor/script review HTML from host frame metadata")
    parser.add_argument("--meta-dir", type=Path, required=True)
    parser.add_argument("--out-html", type=Path, required=True)
    parser.add_argument("--title", default="Frame Meta Review")
    parser.add_argument("--image-width", type=int, default=640)
    parser.add_argument("--image-height", type=int, default=480)
    args = parser.parse_args()

    meta_paths = sorted(args.meta_dir.glob("**/frame_*.json"))
    rows = load_rows(meta_paths, args.image_width, args.image_height)
    args.out_html.parent.mkdir(parents=True, exist_ok=True)
    args.out_html.write_text(build_html(rows, args.title, args.out_html), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
