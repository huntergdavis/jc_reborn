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


def resolve_report_path(path_value: str | None, base_dir: Path) -> Path | None:
    if not path_value:
        return None
    path = Path(path_value)
    if path.is_absolute():
        return path if path.exists() else None
    candidate = (base_dir / path).resolve()
    if candidate.exists():
        return candidate
    return None


def normalize_label(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "", value.lower())


def render_tokens(values: list[str]) -> str:
    if not values:
        return '<span class="muted">none</span>'
    return "".join(f'<span class="pill">{esc(value)}</span>' for value in values)


def classify_headroom(value: object) -> str:
    try:
        numeric = float(value)
    except (TypeError, ValueError):
        return "neutral"
    if numeric <= 1.0:
        return "danger"
    if numeric <= 3.0:
        return "warn"
    return "safe"


def render_metric_card(label: str, value: object, level: str = "neutral") -> str:
    return (
        f'<div class="card metric-card metric-{esc(level)}">'
        f'<div class="label">{esc(label)}</div>'
        f'<div class="value">{esc(value)}</div>'
        "</div>"
    )


def render_tightest_challenge(challenges: dict) -> str:
    query_label = challenges.get("tightest_query_label")
    if not query_label:
        return render_metric_card("Tightest Challenge", "n/a")
    level = classify_headroom(challenges.get("tightest_headroom"))
    return (
        f'<div class="card metric-card metric-{esc(level)}">'
        f'<div class="label">Tightest Challenge</div>'
        f'<div class="value">{esc(query_label)}</div>'
        f'<div class="query-meta">metric={esc(challenges.get("tightest_metric"))} '
        f'value={esc(challenges.get("tightest_value"))}/{esc(challenges.get("tightest_limit"))} '
        f'headroom={esc(challenges.get("tightest_headroom"))} '
        f'pressure={esc(challenges.get("tightest_pressure"))}</div>'
        "</div>"
    )


def challenge_risk_status(challenges: dict) -> tuple[str, str]:
    levels = [
        classify_headroom(challenges.get("unknown_score_headroom")),
        classify_headroom(challenges.get("unknown_margin_headroom")),
        classify_headroom(challenges.get("ambiguous_score_headroom")),
        classify_headroom(challenges.get("ambiguous_margin_headroom")),
    ]
    danger_count = sum(1 for level in levels if level == "danger")
    warn_count = sum(1 for level in levels if level == "warn")
    if danger_count > 0:
        return "elevated_risk", "metric-danger"
    if warn_count > 0:
        return "warning", "metric-warn"
    return "normal", "metric-safe"


def build_scene_index(manifest: dict, manifest_base_dir: Path) -> dict[str, dict]:
    index = {}
    manifest_root = manifest.get("root")
    manifest_root_dir = Path(manifest_root).resolve() if manifest_root else manifest_base_dir.resolve()
    for scene in manifest.get("scenes", []):
        scene_label = str(scene.get("scene_label") or "")
        scene_dir = str(scene.get("scene_dir") or "")
        rows = scene.get("rows") or []
        frame_index = {}
        for row in rows:
            image_path = resolve_report_path(row.get("image_path"), manifest_root_dir)
            if image_path:
                frame_index[int(row.get("frame_number"))] = Path(image_path)
        entry = {
            "scene_dir": scene_dir,
            "scene_path": str((manifest_root_dir / scene_dir).resolve()) if scene_dir else "",
            "frame_index": frame_index,
        }
        index[scene_label] = entry
        index[normalize_label(scene_label)] = entry
        if scene_dir:
            index[scene_dir] = entry
            index[normalize_label(scene_dir)] = entry
    return index


def build_semantic_index(semantic_truth: dict) -> dict[str, dict]:
    index = {}
    for scene in semantic_truth.get("scenes", []):
        scene_label = str(scene.get("scene_label") or "")
        scene_dir = str(scene.get("scene_dir") or "")
        entry = {
            "scene_summary": scene.get("scene_summary") or {},
            "rows": scene.get("rows") or [],
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


def semantic_rows_for_variant(rows: list[dict], variant: str | None) -> list[dict]:
    if not rows:
        return []
    if not variant or variant == "full-redacted":
        return rows
    if variant == "tail-half":
        tail_count = max(2, len(rows) // 2)
        return rows[-tail_count:]
    if variant == "active-only":
        active_rows = [row for row in rows if (row.get("actor_count") or 0) > 0]
        return active_rows or rows[:1]
    if variant == "transition-only":
        transition_rows = [row for row in rows if row.get("state_changed")]
        return transition_rows or rows[:1]
    if variant.startswith("prefix-"):
        try:
            count = int(variant.split("-", 1)[1])
        except ValueError:
            return rows
        return rows[:count]
    return rows


def joined_unique(rows: list[dict], key: str) -> list[str]:
    seen = []
    for row in rows:
        for value in row.get(key) or []:
            if value not in seen:
                seen.append(value)
    return seen


def build_semantic_slice_summary(rows: list[dict]) -> dict[str, str]:
    if not rows:
        return {
            "dominant_state": "n/a",
            "dominant_activity": "n/a",
            "entities": "none",
            "transition_text": "none",
            "timeline": "n/a",
            "frame_count": "0",
            "active_frame_count": "0",
            "state_change_count": "0",
        }
    state_counts: dict[str, int] = {}
    activity_counts: dict[str, int] = {}
    entity_names: list[str] = []
    transitions: list[str] = []
    timeline_parts: list[str] = []
    for row in rows:
        frame_state = str(row.get("frame_state") or "unknown")
        state_counts[frame_state] = state_counts.get(frame_state, 0) + 1
        primary_activity = str(row.get("primary_activity") or "n/a")
        activity_counts[primary_activity] = activity_counts.get(primary_activity, 0) + 1
        timeline_parts.append(f"{row.get('frame_number')}:{frame_state}")
        for entity in row.get("entities") or []:
            if entity not in entity_names:
                entity_names.append(entity)
        if row.get("state_changed"):
            transitions.append(
                f"{row.get('frame_number')}:{row.get('previous_frame_state') or 'start'}->{frame_state}"
            )
    dominant_state = max(state_counts.items(), key=lambda item: (item[1], item[0]))[0]
    dominant_activity = max(activity_counts.items(), key=lambda item: (item[1], item[0]))[0]
    active_frame_count = sum(1 for row in rows if (row.get("actor_count") or 0) > 0)
    state_change_count = sum(1 for row in rows if row.get("state_changed"))
    return {
        "dominant_state": dominant_state,
        "dominant_activity": dominant_activity,
        "entities": ", ".join(entity_names) or "none",
        "transition_text": "; ".join(transitions) if transitions else "none",
        "timeline": " > ".join(timeline_parts),
        "frame_count": str(len(rows)),
        "active_frame_count": str(active_frame_count),
        "state_change_count": str(state_change_count),
    }


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


def build_frame_preview(scene_index: dict[str, dict], output_path: Path, query_label: str) -> str:
    base_label, variant = parse_query_scene_label(query_label)
    scene = scene_index.get(base_label) or scene_index.get(normalize_label(base_label))
    if not scene:
        return ""
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
    cards = []
    for frame_no in selected:
        image_path = frame_index.get(frame_no)
        if not image_path or not image_path.exists():
            continue
        image_href = esc(rel(image_path, output_path.parent))
        cards.append(
            f"""
<a class="thumb-card" href="{image_href}">
  <img class="thumb" src="{image_href}" alt="{esc(base_label)} frame {esc(frame_no)}" loading="lazy">
  <span class="thumb-label">frame {esc(frame_no)}</span>
</a>
"""
        )
    if not cards:
        return ""
    return f'<div class="thumb-strip">{"".join(cards)}</div>'


def build_scene_review_link(scene_index: dict[str, dict], output_path: Path, query_label: str) -> str:
    base_label, _variant = parse_query_scene_label(query_label)
    scene = scene_index.get(base_label) or scene_index.get(normalize_label(base_label))
    if not scene:
        return ""
    scene_path = scene.get("scene_path")
    if not scene_path:
        return ""
    review_path = Path(scene_path) / "review.html"
    if not review_path.exists():
        return ""
    return f'<div class="query-meta"><a href="{esc(rel(review_path, output_path.parent))}">scene review</a></div>'


def build_semantic_summary(semantic_index: dict[str, dict], query_label: str) -> str:
    base_label, variant = parse_query_scene_label(query_label)
    scene = semantic_index.get(base_label) or semantic_index.get(normalize_label(base_label))
    if not scene:
        return ""
    summary = build_semantic_slice_summary(semantic_rows_for_variant(scene.get("rows") or [], variant))
    return (
        '<div class="semantic-summary">'
        f'<div><span class="muted">frames</span> {esc(summary["frame_count"])} <span class="muted">active</span> {esc(summary["active_frame_count"])} <span class="muted">changes</span> {esc(summary["state_change_count"])}</div>'
        f'<div><span class="muted">state</span> {esc(summary["dominant_state"])}</div>'
        f'<div><span class="muted">activity</span> {esc(summary["dominant_activity"])}</div>'
        f'<div><span class="muted">entities</span> {esc(summary["entities"])}</div>'
        f'<div><span class="muted">transitions</span> {esc(summary["transition_text"])}</div>'
        f'<div><span class="muted">timeline</span> {esc(summary["timeline"])}</div>'
        "</div>"
    )


def render_query_cell(
    scene_index: dict[str, dict],
    semantic_index: dict[str, dict],
    output_path: Path,
    query_label: str,
) -> str:
    return (
        f"{esc(query_label)}"
        f'<div class="query-meta">{build_frame_links(scene_index, output_path, query_label)}</div>'
        f"{build_scene_review_link(scene_index, output_path, query_label)}"
        f"{build_semantic_summary(semantic_index, query_label)}"
        f"{build_frame_preview(scene_index, output_path, query_label)}"
    )


def render_evidence_delta(best_evidence: list[str], second_evidence: list[str]) -> tuple[str, str, str]:
    best_set = set(best_evidence)
    second_set = set(second_evidence)
    best_only = [value for value in best_evidence if value not in second_set]
    shared = [value for value in best_evidence if value in second_set]
    second_only = [value for value in second_evidence if value not in best_set]
    return render_tokens(best_only), render_tokens(shared), render_tokens(second_only)


def render_selfcheck(
    rows: list[dict],
    scene_index: dict[str, dict],
    semantic_index: dict[str, dict],
    output_path: Path,
) -> str:
    body = []
    for row in rows:
        ctx = row.get("decision_context") or {}
        profile = ctx.get("query_profile") or {}
        best_only, shared, second_only = render_evidence_delta(
            ctx.get("best_evidence") or [],
            ctx.get("second_evidence") or [],
        )
        second_match = row.get("second_match") or {}
        body.append(
            f"""
<tr>
  <td>{render_query_cell(scene_index, semantic_index, output_path, str(row.get('query_scene_label')))}</td>
  <td>{esc(row.get('identification_status'))}</td>
  <td>{esc(row.get('identification_reason'))}</td>
  <td>{esc(ctx.get('score_margin'))}</td>
  <td>{esc(ctx.get('score_ratio'))}</td>
  <td>{esc(profile.get('active_frame_count'))}/{esc(profile.get('frame_count'))}</td>
  <td>{esc(second_match.get('scene_label') or second_match.get('label') or 'n/a')}</td>
  <td>{best_only}</td>
  <td>{shared}</td>
  <td>{second_only}</td>
</tr>
"""
        )
    return "".join(body)


def render_simple_rows(
    rows: list[dict],
    columns: list[tuple[str, str]],
    scene_index: dict[str, dict],
    semantic_index: dict[str, dict],
    output_path: Path,
) -> str:
    body = []
    for row in rows:
        cells = []
        for key, kind in columns:
            value = row.get(key)
            if kind == "tokens":
                cells.append(f"<td>{render_tokens(value or [])}</td>")
            elif kind == "query":
                cells.append(f"<td>{render_query_cell(scene_index, semantic_index, output_path, str(value))}</td>")
            else:
                cells.append(f"<td>{esc(value)}</td>")
        body.append("<tr>" + "".join(cells) + "</tr>")
    return "".join(body)


def build_html(
    selfcheck: dict,
    partials: dict,
    challenges: dict,
    temporal: dict,
    manifest: dict,
    manifest_base_dir: Path,
    semantic_truth: dict,
    output_path: Path,
    title: str,
) -> str:
    scene_index = build_scene_index(manifest, manifest_base_dir)
    semantic_index = build_semantic_index(semantic_truth)
    challenge_risk_label, challenge_risk_class = challenge_risk_status(challenges)
    top_links = " ".join(
        [
            '<a href="index.html">index.html</a>',
            '<a href="capture-regression-review.html">capture-regression-review.html</a>',
            '<a href="capture-regression-report.json">capture-regression-report.json</a>',
            '<a href="verification-summary.json">verification-summary.json</a>',
            '<a href="verification-summary.txt">verification-summary.txt</a>',
            '<a href="frame-image-regression-report.json">frame-image-regression-report.json</a>',
            '<a href="frame-meta-regression-report.json">frame-meta-regression-report.json</a>',
            '<a href="semantic-regression-report.json">semantic-regression-report.json</a>',
            '<a href="identification-selfcheck.json">identification-selfcheck.json</a>',
            '<a href="identification-eval.json">identification-eval.json</a>',
            '<a href="identification-partials.json">identification-partials.json</a>',
            '<a href="identification-challenges.json">identification-challenges.json</a>',
            '<a href="identification-temporal.json">identification-temporal.json</a>',
            '<a href="semantic-truth.json">semantic-truth.json</a>',
        ]
    )
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
    .metric-warn {{ border-color:#9a6b14; background:#241b0e; }}
    .metric-danger {{ border-color:#a33a3a; background:#2a1212; }}
    .metric-safe {{ border-color:#245d3a; }}
    .risk-banner {{
      margin:14px 0 18px; padding:12px 16px; border-radius:12px; border:1px solid var(--line);
      background:var(--panel);
    }}
    .mini-summary {{
      display:grid; grid-template-columns:repeat(4,minmax(180px,1fr)); gap:12px; margin:12px 0 16px;
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
    .thumb-strip {{
      display:flex; gap:8px; flex-wrap:wrap; margin-top:8px;
    }}
    .thumb-card {{
      display:inline-flex; flex-direction:column; gap:4px; text-decoration:none; color:var(--text);
    }}
    .thumb {{
      width:120px; height:90px; object-fit:cover; border:1px solid var(--line); border-radius:8px;
      background:#0b1117;
    }}
    .thumb-label {{
      color:var(--muted); font-size:11px;
    }}
    .query-meta {{
      margin-top:6px; color:var(--muted);
    }}
    .semantic-summary {{
      margin-top:8px; padding:8px 10px; border:1px solid var(--line); border-radius:8px; background:#101820;
    }}
    .semantic-summary div {{
      margin-top:3px;
    }}
    .top-links {{ margin:0 0 18px 0; display:flex; gap:16px; flex-wrap:wrap; }}
    a {{ color:#8bd5ff; text-decoration:none; }}
  </style>
</head>
<body>
  <main>
    <h1>{esc(title)}</h1>
    <div class="meta">Deterministic matcher review across exact, partial, challenge, and temporal audits.</div>
    <div class="top-links">{top_links}</div>
    <div class="risk-banner {esc(challenge_risk_class)}"><span class="label">Challenge Risk</span><div class="value">{esc(challenge_risk_label)}</div></div>
    <div class="summary">
      {render_metric_card("Exact Min Margin", selfcheck.get('min_identified_margin', 'n/a'))}
      {render_metric_card("Partial Min Margin", partials.get('min_margin', 'n/a'))}
      {render_metric_card("Challenge Non-Identified", challenges.get('query_count', 'n/a'))}
      {render_metric_card("Temporal Max Score Drop", temporal.get('max_score_drop', 'n/a'))}
    </div>

    <section>
      <h2>Exact Selfcheck</h2>
      <table>
        <thead>
          <tr>
            <th>Scene</th><th>Status</th><th>Reason</th><th>Margin</th><th>Ratio</th><th>Active/Frames</th><th>Runner-Up</th><th>Winner Edge</th><th>Shared Evidence</th><th>Runner-Up Only</th>
          </tr>
        </thead>
        <tbody>{render_selfcheck(selfcheck.get('rows') or [], scene_index, semantic_index, output_path)}</tbody>
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
        <tbody>{render_simple_rows(partials.get('rows') or [], [('query_scene_label','query'), ('expected_status','text'), ('status','text'), ('best_scene_label','text'), ('score_margin','text'), ('best_to_second_ratio','text'), ('shared_frame_count','text')], scene_index, semantic_index, output_path)}</tbody>
      </table>
    </section>

    <section>
      <h2>Challenge Queries</h2>
      <div class="meta">ambiguous={esc(challenges.get('ambiguous_count'))} unknown={esc(challenges.get('unknown_count'))}</div>
      <div class="mini-summary">
        {render_tightest_challenge(challenges)}
        {render_metric_card("Unknown Max Score", challenges.get('max_unknown_best_score', 'n/a'))}
        {render_metric_card("Unknown Max Margin", challenges.get('max_unknown_margin', 'n/a'))}
        {render_metric_card("Ambiguous Max Score", challenges.get('max_ambiguous_best_score', 'n/a'))}
        {render_metric_card("Ambiguous Max Margin", challenges.get('max_ambiguous_margin', 'n/a'))}
        {render_metric_card("Unknown Score Headroom", challenges.get('unknown_score_headroom', 'n/a'), classify_headroom(challenges.get('unknown_score_headroom')))}
        {render_metric_card("Unknown Margin Headroom", challenges.get('unknown_margin_headroom', 'n/a'), classify_headroom(challenges.get('unknown_margin_headroom')))}
        {render_metric_card("Ambiguous Score Headroom", challenges.get('ambiguous_score_headroom', 'n/a'), classify_headroom(challenges.get('ambiguous_score_headroom')))}
        {render_metric_card("Ambiguous Margin Headroom", challenges.get('ambiguous_margin_headroom', 'n/a'), classify_headroom(challenges.get('ambiguous_margin_headroom')))}
      </div>
      <table>
        <thead>
          <tr>
            <th>Query</th><th>Expected</th><th>Status</th><th>Best</th><th>Score</th><th>Borrowed Risk</th><th>Borrowed Mismatch</th><th>Score Headroom</th><th>Margin</th><th>Margin Headroom</th><th>Reason</th>
          </tr>
        </thead>
        <tbody>{render_simple_rows(challenges.get('rows') or [], [('query_scene_label','query'), ('expected_status','text'), ('status','text'), ('best_scene_label','text'), ('best_score','text'), ('borrowed_background_risk','text'), ('borrowed_background_mismatch','text'), ('score_headroom','text'), ('score_margin','text'), ('margin_headroom','text'), ('reason','text')], scene_index, semantic_index, output_path)}</tbody>
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
        <tbody>{render_simple_rows(temporal.get('rows') or [], [('query_scene_label','query'), ('expected_status','text'), ('status','text'), ('best_scene_label','text'), ('best_score','text'), ('score_margin','text'), ('best_to_second_ratio','text')], scene_index, semantic_index, output_path)}</tbody>
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
    parser.add_argument("--semantic-truth-json", type=Path, required=True)
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
    semantic_truth = load_json(args.semantic_truth_json)

    args.out_html.parent.mkdir(parents=True, exist_ok=True)
    args.out_html.write_text(
        build_html(
            summary,
            partials,
            challenges,
            temporal,
            manifest,
            args.manifest_json.parent,
            semantic_truth,
            args.out_html,
            args.title,
        ),
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
