#!/usr/bin/env python3
from __future__ import annotations

import argparse
import html
import json
from pathlib import Path


def esc(value: object) -> str:
    return html.escape(str(value))


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def render_tokens(values: list[str]) -> str:
    if not values:
        return '<span class="muted">none</span>'
    return "".join(f'<span class="pill">{esc(value)}</span>' for value in values)


def render_selfcheck(rows: list[dict]) -> str:
    body = []
    for row in rows:
        ctx = row.get("decision_context") or {}
        profile = ctx.get("query_profile") or {}
        body.append(
            f"""
<tr>
  <td>{esc(row.get('query_scene_label'))}</td>
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


def render_simple_rows(rows: list[dict], columns: list[tuple[str, str]]) -> str:
    body = []
    for row in rows:
        cells = []
        for key, kind in columns:
            value = row.get(key)
            if kind == "tokens":
                cells.append(f"<td>{render_tokens(value or [])}</td>")
            else:
                cells.append(f"<td>{esc(value)}</td>")
        body.append("<tr>" + "".join(cells) + "</tr>")
    return "".join(body)


def build_html(selfcheck: dict, partials: dict, challenges: dict, temporal: dict, title: str) -> str:
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
        <tbody>{render_selfcheck(selfcheck.get('rows') or [])}</tbody>
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
        <tbody>{render_simple_rows(partials.get('rows') or [], [('query_scene_label','text'), ('expected_status','text'), ('status','text'), ('best_scene_label','text'), ('score_margin','text'), ('best_to_second_ratio','text'), ('shared_frame_count','text')])}</tbody>
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
        <tbody>{render_simple_rows(challenges.get('rows') or [], [('query_scene_label','text'), ('expected_status','text'), ('status','text'), ('best_scene_label','text'), ('best_score','text'), ('score_margin','text'), ('reason','text')])}</tbody>
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
        <tbody>{render_simple_rows(temporal.get('rows') or [], [('query_scene_label','text'), ('expected_status','text'), ('status','text'), ('best_scene_label','text'), ('best_score','text'), ('score_margin','text'), ('best_to_second_ratio','text')])}</tbody>
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
    parser.add_argument("--out-html", type=Path, required=True)
    parser.add_argument("--title", default="Host Identification Review")
    args = parser.parse_args()

    selfcheck = load_json(args.selfcheck_json)
    summary = load_json(args.eval_json)
    summary["rows"] = selfcheck.get("rows") or []
    partials = load_json(args.partials_json)
    challenges = load_json(args.challenges_json)
    temporal = load_json(args.temporal_json)

    args.out_html.parent.mkdir(parents=True, exist_ok=True)
    args.out_html.write_text(
        build_html(summary, partials, challenges, temporal, args.title),
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
