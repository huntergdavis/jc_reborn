#!/usr/bin/env python3
from __future__ import annotations

import argparse
import html
import json
import os
from pathlib import Path
from typing import Any


def esc(value: Any) -> str:
    return html.escape(str(value))


def rel(target: Path, out_html: Path) -> str:
    return os.path.relpath(target.resolve(), out_html.parent.resolve())


def load(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def resolve_capture_path(path_value: str, base_dir: Path) -> Path:
    path = Path(path_value)
    if path.is_absolute():
        return path.resolve()
    return (base_dir / path).resolve()


def pill_class(label: str) -> str:
    if label == "correct_scene_content":
        return "good"
    if label == "broken_scene_output":
        return "bad"
    return "neutral"


def render_case(case: dict[str, Any], analysis: dict[str, Any], analysis_path: Path, out_html: Path) -> str:
    meta = analysis.get("_meta", {})
    reference_image = resolve_capture_path(meta["reference_image"], analysis_path.parent)
    query_image = resolve_capture_path(meta["query_image"], analysis_path.parent)
    reference_state = meta.get("reference_state", {})
    query_state = meta.get("query_state", {})
    expected = case["expected_label"]
    actual = case["actual_label"]
    missing = case.get("missing_characters", [])
    analysis_link_path = (out_html.parent / case["analysis_json"]).resolve()
    passed = bool(case.get("passed"))
    header_class = "pass" if passed else "fail"
    human_status = case.get("human_review_status") or "unreviewed"
    human_notes = case.get("human_review_notes") or ""

    return f"""
<section class="card {header_class}" id="{esc(case['case_id'])}">
  <div class="card-head">
    <div>
      <h2>{esc(case['case_id'])}</h2>
      <div class="meta">{esc(case['scene_id'])} · {esc(case['family'])} · {esc(case['source_kind'])}</div>
    </div>
    <div class="stats">
      <span class="pill {pill_class(expected)}">expected: {esc(expected)}</span>
      <span class="pill {pill_class(actual)}">model: {esc(actual)}</span>
      <span class="pill {'ok' if passed else 'warn'}">{'match' if passed else 'mismatch'}</span>
      <span class="pill neutral">human: {esc(human_status)}</span>
    </div>
  </div>
  <div class="grid">
    <figure>
      <figcaption>Reference</figcaption>
      <img loading="lazy" src="{esc(rel(reference_image, out_html))}" alt="{esc(reference_image.name)}">
      <div class="path">{esc(reference_image)}</div>
      <div class="notes">
        <div><strong>state:</strong> {esc(reference_state.get('screen_type', 'unknown'))}</div>
        <div><strong>reason:</strong> {esc(reference_state.get('reason', ''))}</div>
      </div>
    </figure>
    <figure>
      <figcaption>Query</figcaption>
      <img loading="lazy" src="{esc(rel(query_image, out_html))}" alt="{esc(query_image.name)}">
      <div class="path">{esc(query_image)}</div>
      <div class="notes">
        <div><strong>state:</strong> {esc(query_state.get('screen_type', 'unknown'))}</div>
        <div><strong>reason:</strong> {esc(query_state.get('reason', ''))}</div>
      </div>
    </figure>
  </div>
  <div class="summary-grid">
    <div class="box"><div class="k">Same Scene State</div><div class="v">{esc(case.get('same_scene_state'))}</div></div>
    <div class="box"><div class="k">Reference Screen</div><div class="v">{esc(case.get('reference_screen_type'))}</div></div>
    <div class="box"><div class="k">Query Screen</div><div class="v">{esc(case.get('query_screen_type'))}</div></div>
    <div class="box"><div class="k">Missing Characters</div><div class="v">{esc(', '.join(missing) or 'none')}</div></div>
  </div>
  <div class="text-block">
    <div class="k">Model Summary</div>
    <div class="v">{esc(case.get('summary', ''))}</div>
  </div>
  <div class="text-block">
    <div class="k">Human Review Notes</div>
    <div class="v">{esc(human_notes or 'none')}</div>
  </div>
  <div class="text-block">
    <div class="k">Raw Compare Response</div>
    <pre>{esc(meta.get('raw_response', ''))}</pre>
  </div>
  <div class="links">
    <a href="{esc(rel(analysis_link_path, out_html))}">Analysis JSON</a>
  </div>
</section>
"""


def build_html(title: str, summary: dict[str, Any], out_html: Path) -> str:
    summary_path = out_html.parent / "validation-summary.json"
    cards = []
    for case in summary.get("cases", []):
        analysis_path = (out_html.parent / case["analysis_json"]).resolve()
        analysis = load(analysis_path)
        cards.append(render_case(case, analysis, analysis_path, out_html))

    family_rows = []
    for family, stats in summary.get("family_stats", {}).items():
        family_rows.append(
            f"<tr><td>{esc(family)}</td><td>{esc(stats['case_count'])}</td><td>{esc(stats['passed_count'])}</td><td>{esc(stats['accuracy'])}</td></tr>"
        )
    source_rows = []
    for kind, stats in summary.get("source_kind_stats", {}).items():
        source_rows.append(
            f"<tr><td>{esc(kind)}</td><td>{esc(stats['case_count'])}</td><td>{esc(stats['passed_count'])}</td><td>{esc(stats['accuracy'])}</td></tr>"
        )

    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>{esc(title)}</title>
  <style>
    :root {{
      --bg:#0f1317;
      --panel:#171d24;
      --panel2:#0f141a;
      --text:#e8edf2;
      --muted:#94a3b3;
      --border:#2b3642;
      --good:#7bd389;
      --bad:#ff8a7a;
      --warn:#f0c36a;
      --link:#8cc8ff;
    }}
    body {{ margin:0; background:var(--bg); color:var(--text); font:14px/1.45 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace; }}
    main {{ max-width:1600px; margin:0 auto; padding:24px; }}
    h1,h2,p {{ margin:0; }}
    .top {{ margin-bottom:24px; }}
    .summary {{ color:var(--muted); margin-top:8px; max-width:1000px; }}
    .summary-grid {{ display:grid; grid-template-columns: repeat(4, minmax(0,1fr)); gap:12px; margin:16px 0; }}
    .box, .table-box {{ background:var(--panel); border:1px solid var(--border); border-radius:12px; padding:12px; }}
    .box .k, .text-block .k {{ color:var(--muted); font-size:12px; margin-bottom:6px; }}
    .box .v {{ font-size:18px; }}
    .card {{ background:var(--panel); border:1px solid var(--border); border-radius:14px; padding:18px; margin-bottom:18px; }}
    .card.pass {{ border-color:#244030; }}
    .card.fail {{ border-color:#5a2d2d; }}
    .card-head {{ display:flex; justify-content:space-between; gap:16px; margin-bottom:12px; }}
    .meta {{ color:var(--muted); margin-top:6px; }}
    .stats {{ display:flex; gap:8px; flex-wrap:wrap; }}
    .pill {{ border:1px solid var(--border); border-radius:999px; padding:6px 10px; }}
    .pill.good, .pill.ok {{ color:var(--good); border-color:#2a5a38; }}
    .pill.bad {{ color:var(--bad); border-color:#6a2f2f; }}
    .pill.warn, .pill.neutral {{ color:var(--warn); border-color:#6c5930; }}
    .grid {{ display:grid; grid-template-columns: 1fr 1fr; gap:16px; margin-bottom:16px; }}
    figure {{ margin:0; background:var(--panel2); border:1px solid var(--border); border-radius:10px; overflow:hidden; }}
    figcaption {{ padding:10px 12px; border-bottom:1px solid var(--border); color:var(--warn); }}
    img {{ display:block; width:100%; height:auto; image-rendering:pixelated; background:#000; }}
    .path {{ padding:10px 12px 0; color:var(--muted); font-size:12px; word-break:break-all; }}
    .notes {{ padding:10px 12px 12px; }}
    .text-block {{ background:var(--panel2); border:1px solid var(--border); border-radius:10px; padding:12px; margin-top:12px; }}
    pre {{ white-space:pre-wrap; word-break:break-word; margin:0; color:#cfd8e3; }}
    .links {{ margin-top:12px; }}
    a {{ color:var(--link); }}
    table {{ width:100%; border-collapse:collapse; }}
    th, td {{ border-top:1px solid var(--border); padding:10px 12px; text-align:left; }}
    th {{ color:var(--muted); }}
    @media (max-width: 1100px) {{
      .summary-grid, .grid {{ grid-template-columns:1fr; }}
      .card-head {{ flex-direction:column; }}
    }}
  </style>
</head>
<body>
  <main>
    <div class="top">
      <h1>{esc(title)}</h1>
      <p class="summary">Hand-review page for the current VLM trust pack. Each card shows the exact reference image, the exact query image, the expected label we assigned, the model label it produced, and the model’s own reasons. Reply in chat with any case IDs that look wrong.</p>
      <div class="summary-grid">
        <div class="box"><div class="k">Cases</div><div class="v">{esc(summary.get('case_count'))}</div></div>
        <div class="box"><div class="k">Passed</div><div class="v">{esc(summary.get('passed_count'))}</div></div>
        <div class="box"><div class="k">Failed</div><div class="v">{esc(summary.get('failed_count'))}</div></div>
        <div class="box"><div class="k">Accuracy</div><div class="v">{esc(summary.get('accuracy'))}</div></div>
      </div>
      <div class="grid">
        <div class="table-box">
          <div class="k">Family Stats</div>
          <table>
            <thead><tr><th>Family</th><th>Cases</th><th>Passed</th><th>Accuracy</th></tr></thead>
            <tbody>{''.join(family_rows)}</tbody>
          </table>
        </div>
        <div class="table-box">
          <div class="k">Source Stats</div>
          <table>
            <thead><tr><th>Source</th><th>Cases</th><th>Passed</th><th>Accuracy</th></tr></thead>
            <tbody>{''.join(source_rows)}</tbody>
          </table>
        </div>
      </div>
      <p class="summary">Summary JSON: <a href="{esc(rel(summary_path.resolve(), out_html))}">{esc(summary_path.name)}</a></p>
    </div>
    {''.join(cards)}
  </main>
</body>
</html>
"""


def main() -> None:
    parser = argparse.ArgumentParser(description="Render a hand-review HTML page for the VLM scene pack.")
    parser.add_argument("--summary-json", type=Path, required=True)
    parser.add_argument("--out-html", type=Path, required=True)
    parser.add_argument("--title", type=str, default="PS1 VLM Scene Pack Review")
    args = parser.parse_args()

    summary = load(args.summary_json.resolve())
    args.out_html.parent.mkdir(parents=True, exist_ok=True)
    args.out_html.write_text(build_html(args.title, summary, args.out_html.resolve()), encoding="utf-8")


if __name__ == "__main__":
    main()
