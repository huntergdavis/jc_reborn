#!/usr/bin/env python3
from __future__ import annotations

import argparse
import html
import importlib.util
import json
import os
from pathlib import Path


def load_summarize():
    script_path = Path(__file__).with_name("summarize-frame-meta.py")
    spec = importlib.util.spec_from_file_location("summarize_frame_meta", script_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load summarizer from {script_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.summarize


summarize = load_summarize()


def esc(value: object) -> str:
    return html.escape(str(value))


def rel(path: Path, dst_dir: Path) -> str:
    return os.path.relpath(path.resolve(), dst_dir.resolve())


def scene_rows(scene_dir: Path) -> tuple[dict, list[dict]]:
    rows = []
    scene_label = scene_dir.name
    for meta_path in sorted((scene_dir / "frame-meta").glob("frame_*.json")):
        summary = summarize(meta_path)
        image_path = Path(json.loads(meta_path.read_text(encoding="utf-8"))["image_path"])
        rows.append(
            {
                "frame_number": summary["frame_number"],
                "frame_name": image_path.name,
                "image_path": str(image_path.resolve()),
                "actor_summary": summary["actor_summary"],
                "actor_candidate_draw_count": summary["actor_candidate_draw_count"],
                "visible_unique_draw_count": summary["visible_unique_draw_count"],
                "bmp_names": [row["bmp_name"] for row in summary["actor_candidates"]],
            }
        )
        if summary.get("scene_label"):
            scene_label = str(summary["scene_label"])
    info = {
        "scene_dir": scene_dir.name,
        "scene_label": scene_label,
        "frame_count": len(rows),
        "review_html": str((scene_dir / "review.html").resolve()),
        "rows": rows,
    }
    return info, rows


def build_manifest(root: Path) -> dict:
    scenes = []
    for scene_dir in sorted(p for p in root.iterdir() if p.is_dir()):
        info, _ = scene_rows(scene_dir)
        scenes.append(info)
    extras = {}
    for name in ("identification-review.html", "capture-regression-review.html"):
        path = root / name
        if not path.is_file():
            raise FileNotFoundError(f"required dashboard missing: {path}")
        extras[name] = str(path.resolve())
    return {"root": str(root.resolve()), "scenes": scenes, "extras": extras}


def write_manifest(manifest: dict, path: Path) -> None:
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def build_html(manifest: dict, output_path: Path, title: str) -> str:
    extras = manifest.get("extras", {})
    top_links = []
    for name, label in (
        ("identification-review.html", "Identification Review"),
        ("capture-regression-review.html", "Capture Regression Review"),
    ):
        path = extras.get(name)
        if path:
            top_links.append(f'<a href="{esc(rel(Path(path), output_path.parent))}">{esc(label)}</a>')

    cards = []
    for scene in manifest["scenes"]:
        review_path = Path(scene["review_html"])
        review_href = rel(review_path, output_path.parent) if review_path.exists() else None
        rows_html = []
        for row in scene["rows"]:
            image_rel = rel(Path(row["image_path"]), output_path.parent)
            actor_summary = ", ".join(f"{k}={v}" for k, v in sorted(row["actor_summary"].items())) or "none"
            bmp_names = ", ".join(row["bmp_names"]) or "none"
            rows_html.append(
                f"""
<tr>
  <td><a href="{esc(image_rel)}">{esc(row['frame_name'])}</a></td>
  <td>{esc(row['frame_number'])}</td>
  <td>{esc(actor_summary)}</td>
  <td>{esc(row['actor_candidate_draw_count'])}</td>
  <td>{esc(row['visible_unique_draw_count'])}</td>
  <td>{esc(bmp_names)}</td>
</tr>
"""
            )
        review_link = f'<a href="{esc(review_href)}">scene review</a>' if review_href else "scene review missing"
        cards.append(
            f"""
<section class="card">
  <div class="head">
    <div>
      <h2>{esc(scene['scene_label'])}</h2>
      <div class="meta">{esc(scene['scene_dir'])} • frames={esc(scene['frame_count'])}</div>
    </div>
    <div class="meta">{review_link}</div>
  </div>
  <table>
    <thead>
      <tr>
        <th>Frame</th>
        <th>No</th>
        <th>Actor Summary</th>
        <th>Actor Candidates</th>
        <th>Visible Unique</th>
        <th>Actor BMPs</th>
      </tr>
    </thead>
    <tbody>
      {''.join(rows_html)}
    </tbody>
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
      --bg: #0f1419;
      --panel: #182028;
      --line: #31404d;
      --text: #e6edf3;
      --muted: #9aabb8;
      --accent: #f2cc60;
    }}
    body {{
      margin: 24px;
      background: linear-gradient(180deg, #111821 0%, #0c1117 100%);
      color: var(--text);
      font: 14px/1.45 ui-monospace, SFMono-Regular, Menlo, monospace;
    }}
    main {{ max-width: 1280px; margin: 0 auto; }}
    .intro {{ color: var(--muted); margin-bottom: 18px; }}
    .top-links {{ margin: 0 0 18px 0; display: flex; gap: 16px; flex-wrap: wrap; }}
    .card {{
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 12px;
      padding: 18px;
      margin-bottom: 18px;
    }}
    .head {{
      display: flex;
      justify-content: space-between;
      gap: 16px;
      margin-bottom: 12px;
      align-items: baseline;
    }}
    .meta {{ color: var(--muted); }}
    a {{ color: #8bd5ff; text-decoration: none; }}
    table {{ width: 100%; border-collapse: collapse; }}
    th, td {{
      text-align: left;
      padding: 8px 10px;
      border-top: 1px solid var(--line);
      vertical-align: top;
    }}
    th {{ color: var(--accent); }}
  </style>
</head>
<body>
  <main>
    <h1>{esc(title)}</h1>
    <div class="intro">Cross-scene host script manifest built from frame metadata summaries.</div>
    <div class="top-links">{''.join(top_links)}</div>
    {''.join(cards)}
  </main>
</body>
</html>
"""


def main() -> int:
    parser = argparse.ArgumentParser(description="Render a cross-scene host script index and manifest")
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--out-html", type=Path, required=True)
    parser.add_argument("--out-json", type=Path, required=True)
    parser.add_argument("--title", default="Host Script Review Index")
    args = parser.parse_args()

    manifest = build_manifest(args.root)
    args.out_html.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    write_manifest(manifest, args.out_json)
    args.out_html.write_text(build_html(manifest, args.out_html, args.title), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
