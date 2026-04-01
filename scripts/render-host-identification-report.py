#!/usr/bin/env python3
from __future__ import annotations

import argparse
import html
import json
import os
import re
from pathlib import Path


def esc(value: object) -> str:
    return html.escape(str(value))


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def rel(path: Path, dst_dir: Path) -> str:
    return os.path.relpath(path.resolve(), dst_dir.resolve())


def normalize_label(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "", value.lower())


def render_tokens(values: list[str]) -> str:
    if not values:
        return '<span class="muted">none</span>'
    return "".join(f'<span class="pill">{esc(value)}</span>' for value in values)


def build_scene_index(manifest: dict) -> dict[str, dict]:
    index = {}
    for scene in manifest.get("scenes", []):
        scene_label = str(scene.get("scene_label") or "")
        scene_dir = str(scene.get("scene_dir") or "")
        rows = scene.get("rows") or []
        frame_index = {}
        for row in rows:
            image_path = row.get("image_path")
            if image_path:
                frame_index[int(row.get("frame_number"))] = Path(image_path)
        entry = {
            "scene_dir": scene_dir,
            "frame_index": frame_index,
        }
        index[scene_label] = entry
        index[normalize_label(scene_label)] = entry
        if scene_dir:
            index[scene_dir] = entry
            index[normalize_label(scene_dir)] = entry
    return index


def parse_query_scene_label(label: str) -> tuple[str, str | None]:
    match = re.match(r"^(.*?)(?: \[(.+)\])?$", label)
    if not match:
        return label, None
    return match.group(1), match.group(2)


def frame_numbers_for_variant(scene_rows: list[int], variant: str | None) -> list[int]:
    if not scene_rows:
        return []
    if not variant:
        return scene_rows
    if variant == "full-redacted":
        return scene_rows
    if variant == "tail-half":
        tail_count = max(2, len(scene_rows) // 2)
        return scene_rows[-tail_count:]
    if variant == "active-only":
        return []
    if variant == "transition-only":
        return []
    if variant.startswith("prefix-"):
        try:
            count = int(variant.split("-", 1)[1])
        except ValueError:
            return scene_rows
        return scene_rows[:count]
    return scene_rows


def build_frame_links(scene_index: dict[str, dict], output_path: Path, query_label: str) -> str:
    base_label, variant = parse_query_scene_label(query_label)
    scene = scene_index.get(base_label) or scene_index.get(normalize_label(base_label))
    if not scene:
        return '<span class="muted">none</span>'
    frame_index = scene["frame_index"]
    frame_numbers = sorted(frame_index)
    selected = frame_numbers_for_variant(frame_numbers, variant)
    if variant == "active-only":
        selected = [frame for frame in frame_numbers if frame_index.get(frame)]
    if variant == "transition-only":
        selected = frame_numbers[:]
    if not selected:
        selected = frame_numbers[:2]
    selected = selected[:4]
    links = []
    for frame_no in selected:
        image_path = frame_index.get(frame_no)
        if image_path and image_path.exists():
            links.append(
                f'<a href="{esc(rel(image_path, output_path.parent))}">frame {esc(frame_no)}</a>'
            )
    return " ".join(links) if links else '<span class="muted">none</span>'


def render_selfcheck(rows: list[dict], scene_index: dict[str, dict], output_path: Path) -> str:
    body = []
    for row in rows:
        ctx = row.get("decision_context") or {}
        profile = ctx.get("query_profile") or {}
        body.append(
            f"""
<tr>
  <td>{esc(row.get('query_scene_label'))}<div>{build_frame_links(scene_index, output_path, str(row.get('query_scene_label')))}</div></td>
  <td>{esc(row.get('identification_status'))}</td>
  <td>{esc(row.get('identification_reason'))}</td>
  <td>{esc(ctx.get('score_margin'))}</td>
  <td>{esc(ctx.get('score_ratio'))}</td>
  <td>{esc(profile.get('active_frame_count'))}/{esc(profile.get('frame_count'))}</td>
  <td>{render_tokens(ctx.get('best_evidence') or [])}</td>
  <td>{render_tokens(ctx.get('second_evidence') or [])}</td>
</tr>
"""
        )
    return "".join(body)


def render_simple_rows(rows: list[dict], columns: list[tuple[str, str]], scene_index: dict[str, dict], output_path: Path) -> str:
    body = []
    for row in rows:
        cells = []
        for key, kind in columns:
            value = row.get(key)
            if kind == "tokens":
                cells.append(f"<td>{render_tokens(value or [])}</td>")
            elif kind == "query":
                cells.append(
                    f"<td>{esc(value)}<div>{build_frame_links(scene_index, output_path, str(value))}</div></td>"
                )
            else:
                cells.append(f"<td>{esc(value)}</td>")
        body.append("<tr>" + "".join(cells) + "</tr>")
    return "".join(body)


def build_html(selfcheck: dict, partials: dict, challenges: dict, temporal: dict, manifest: dict, output_path: Path, title: str) -> str:
    scene_index = build_scene_index(manifest)
    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>{esc(title)}</title>
  <style>
    :root {{
      --bg:#0e141a; --panel:#15202b; --line:#2d3d4c; --text:#eaf1f7; --muted:#9cb0c2;
      --accent:#f0c36b; --pill:#213243; --good:#d3f9d8; --warn:#ffe7a6;
    }}
    body {{
      margin:24px;
      background:linear-gradient(180deg,#0d141b 0%,#101923 100%);
      color:var(--text);
      font:14px/1.45 ui-monospace,SFMono-Regular,Menlo,monospace;
    }}
    main {{ max-width: 1520px; margin: 0 auto; }}
    .summary {{
      display:grid; grid-template-columns:repeat(4,minmax(180px,1fr)); gap:12px; margin:18px 0 24px;
    }}
    .card {{
      background:var(--panel); border:1px solid var(--line); border-radius:12px; padding:14px 16px;
    }}
    .label {{ color:var(--muted); font-size:12px; text-transform:uppercase; letter-spacing:.08em; }}
    .value {{ font-size:20px; color:var(--accent); margin-top:6px; }}
    section {{ margin-top: 22px; }}
    h1,h2 {{ margin:0 0 12px; }}
    .meta {{ color:var(--muted); margin-bottom: 12px; }}
    table {{ width:100%; border-collapse:collapse; background:var(--panel); }}
    th,td {{ border:1px solid var(--line); text-align:left; padding:8px 10px; vertical-align:top; }}
    th {{ color:var(--accent); background:#182532; }}
    .pill {{
      display:inline-block; padding:2px 8px; margin:2px 6px 2px 0; border-radius:999px;
      background:var(--pill); color:var(--good);
    }}
    .muted {{ color:var(--muted); }}
  </style>
</head>
<body>
  <main>
    <h1>{esc(title)}</h1>
    <div class="meta">Deterministic matcher review across exact, partial, challenge, and temporal audits.</div>
    <div class="summary">
      <div class="card"><div class="label">Exact Min Margin</div><div class="value">{esc(selfcheck.get('min_identified_margin', 'n/a'))}</div></div>
      <div class="card"><div class="label">Partial Min Margin</div><div class="value">{esc(partials.get('min_margin', 'n/a'))}</div></div>
      <div class="card"><div class="label">Challenge Non-Identified</div><div class="value">{esc(challenges.get('query_count', 'n/a'))}</div></div>
      <div class="card"><div class="label">Temporal Max Score Drop</div><div class="value">{esc(temporal.get('max_score_drop', 'n/a'))}</div></div>
    </div>

    <section>
      <h2>Exact Selfcheck</h2>
      <table>
        <thead>
          <tr>
            <th>Scene</th><th>Status</th><th>Reason</th><th>Margin</th><th>Ratio</th><th>Active/Frames</th><th>Best Evidence</th><th>Runner-Up Evidence</th>
          </tr>
        </thead>
        <tbody>{render_selfcheck(selfcheck.get('rows') or [], scene_index, output_path)}</tbody>
      </table>
    </section>

    <section>
      <h2>Partial Queries</h2>
      <table>
        <thead>
          <tr>
            <th>Query</th><th>Expected</th><th>Status</th><th>Best</th><th>Margin</th><th>Ratio</th><th>Frames</th>
          </tr>
        </thead>
        <tbody>{render_simple_rows(partials.get('rows') or [], [('query_scene_label','query'), ('expected_status','text'), ('status','text'), ('best_scene_label','text'), ('score_margin','text'), ('best_to_second_ratio','text'), ('shared_frame_count','text')], scene_index, output_path)}</tbody>
      </table>
    </section>

    <section>
      <h2>Challenge Queries</h2>
      <div class="meta">ambiguous={esc(challenges.get('ambiguous_count'))} unknown={esc(challenges.get('unknown_count'))}</div>
      <table>
        <thead>
          <tr>
            <th>Query</th><th>Expected</th><th>Status</th><th>Best</th><th>Score</th><th>Margin</th><th>Reason</th>
          </tr>
        </thead>
        <tbody>{render_simple_rows(challenges.get('rows') or [], [('query_scene_label','query'), ('expected_status','text'), ('status','text'), ('best_scene_label','text'), ('best_score','text'), ('score_margin','text'), ('reason','text')], scene_index, output_path)}</tbody>
      </table>
    </section>

    <section>
      <h2>Temporal Prefixes</h2>
      <div class="meta">max_score_drop={esc(temporal.get('max_score_drop'))} max_identified_margin_drop={esc(temporal.get('max_identified_margin_drop'))}</div>
      <table>
        <thead>
          <tr>
            <th>Query</th><th>Expected</th><th>Status</th><th>Best</th><th>Score</th><th>Margin</th><th>Ratio</th>
          </tr>
        </thead>
        <tbody>{render_simple_rows(temporal.get('rows') or [], [('query_scene_label','query'), ('expected_status','text'), ('status','text'), ('best_scene_label','text'), ('best_score','text'), ('score_margin','text'), ('best_to_second_ratio','text')], scene_index, output_path)}</tbody>
      </table>
    </section>
  </main>
</body>
</html>
"""


def main() -> int:
    parser = argparse.ArgumentParser(description="Render host scene identification audit report as HTML")
    parser.add_argument("--selfcheck-json", type=Path, required=True)
    parser.add_argument("--eval-json", type=Path, required=True)
    parser.add_argument("--partials-json", type=Path, required=True)
    parser.add_argument("--challenges-json", type=Path, required=True)
    parser.add_argument("--temporal-json", type=Path, required=True)
    parser.add_argument("--manifest-json", type=Path, required=True)
    parser.add_argument("--out-html", type=Path, required=True)
    parser.add_argument("--title", default="Host Identification Review")
    args = parser.parse_args()

    selfcheck = load_json(args.selfcheck_json)
    summary = load_json(args.eval_json)
    summary["rows"] = selfcheck.get("rows") or []
    partials = load_json(args.partials_json)
    challenges = load_json(args.challenges_json)
    temporal = load_json(args.temporal_json)
    manifest = load_json(args.manifest_json)

    args.out_html.parent.mkdir(parents=True, exist_ok=True)
    args.out_html.write_text(
        build_html(summary, partials, challenges, temporal, manifest, args.out_html, args.title),
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
