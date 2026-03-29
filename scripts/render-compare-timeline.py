#!/usr/bin/env python3
from __future__ import annotations

import argparse
import html
import json
from pathlib import Path


def relpath(target: Path, start: Path) -> str:
    return str(target.resolve().relative_to(start.resolve().parent.parent.parent.parent.parent)) if False else str(
        Path(
            __import__("os").path.relpath(target.resolve(), start.resolve().parent)
        )
    )


def fmt_int(value):
    if value is None:
        return "n/a"
    return f"{value:,}"


def load_run_frames(result_json_path: Path) -> list[Path]:
    if result_json_path.is_dir():
        frames_dir = find_best_frame_dir(result_json_path)
        if frames_dir.is_dir():
            return sorted(frames_dir.glob("frame_*.*"))
        return []

    try:
        data = json.loads(result_json_path.read_text())
    except Exception:
        return []
    frames_dir = data.get("paths", {}).get("frames_dir")
    if frames_dir:
        return sorted(Path(frames_dir).glob("frame_*.*"))

    fallback_dir = find_best_frame_dir(result_json_path.parent)
    if fallback_dir.is_dir():
        return sorted(fallback_dir.glob("frame_*.*"))
    return []


def render_frame_strip(frames: list[Path], output_path: Path, label: str) -> str:
    if not frames:
        return f"""
<section class="card">
  <div class="card-head">
    <div>
      <h2>{html.escape(label)}</h2>
      <div class="meta">No frames found.</div>
    </div>
  </div>
</section>
"""

    items = []
    for frame in frames:
        items.append(
            f"""
<figure class="timeline-figure">
  <figcaption><code>{html.escape(frame.name)}</code></figcaption>
  <img loading="lazy" src="{html.escape(__import__('os').path.relpath(frame.resolve(), output_path.parent))}" alt="{html.escape(frame.name)}">
  <div class="path">{html.escape(str(frame.resolve()))}</div>
</figure>
"""
        )

    return f"""
<section class="card">
  <div class="card-head">
    <div>
      <h2>{html.escape(label)}</h2>
      <div class="meta">{fmt_int(len(frames))} raw frames in chronological order.</div>
    </div>
  </div>
  <div class="timeline-grid">
    {''.join(items)}
  </div>
</section>
"""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--compare-json", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--title", default="Sequence Timeline")
    args = ap.parse_args()

    compare_path = Path(args.compare_json).resolve()
    output_path = Path(args.output).resolve()

    with compare_path.open() as f:
        data = json.load(f)

    frames = data.get("frames", [])
    result_json_path = Path(data["result"]).resolve() if data.get("result") else None
    reference_json_path = Path(data["reference"]).resolve() if data.get("reference") else None
    result_run_frames = load_run_frames(result_json_path) if result_json_path else []
    reference_run_frames = load_run_frames(reference_json_path) if reference_json_path else []
    output_path.parent.mkdir(parents=True, exist_ok=True)

    cards = []
    for idx, frame in enumerate(frames, start=1):
        result_frame = Path(frame["result_frame"]).resolve()
        ref_frame = Path(frame["reference_frame"]).resolve()
        diff = frame.get("palette_index_diff_pixels")
        status = frame.get("status", "unknown")
        diff_class = "good" if diff == 0 else "bad"
        cards.append(
            f"""
<section class="card" id="pair-{idx}">
  <div class="card-head">
    <div>
      <h2>Pair {idx}</h2>
      <div class="meta">PS1 <code>{html.escape(frame.get("result_frame_name", ""))}</code> ↔ Host <code>{html.escape(frame.get("reference_frame_name", ""))}</code></div>
    </div>
    <div class="stats">
      <span class="pill">{html.escape(status)}</span>
      <span class="pill {diff_class}">diff {fmt_int(diff)}</span>
    </div>
  </div>
  <div class="grid">
    <figure>
      <figcaption>PS1</figcaption>
      <img loading="lazy" src="{html.escape(__import__('os').path.relpath(result_frame, output_path.parent))}" alt="PS1 frame {html.escape(frame.get('result_frame_name', ''))}">
      <div class="path">{html.escape(str(result_frame))}</div>
    </figure>
    <figure>
      <figcaption>Host</figcaption>
      <img loading="lazy" src="{html.escape(__import__('os').path.relpath(ref_frame, output_path.parent))}" alt="Host frame {html.escape(frame.get('reference_frame_name', ''))}">
      <div class="path">{html.escape(str(ref_frame))}</div>
    </figure>
  </div>
</section>
"""
        )

    result_only = "\n".join(f"<li><code>{html.escape(x)}</code></li>" for x in data.get("result_only_frames", []))
    ref_only = "\n".join(f"<li><code>{html.escape(x)}</code></li>" for x in data.get("reference_only_frames", []))

    if frames:
        cards_html = "".join(cards)
    else:
        cards_html = f"""
<section class="card">
  <div class="card-head">
    <div>
      <h2>No Verified Scene Timeline</h2>
      <div class="meta">The compare bundle did not find any PS1 frame that was both post-title and close enough to the host scene sequence to qualify as a verified scene-entry anchor.</div>
    </div>
    <div class="stats">
      <span class="pill bad">{html.escape(str(data.get("error", "no aligned frames")))}</span>
    </div>
  </div>
  <div class="box">
    <div class="k">What this means</div>
    <div class="v" style="font-size:14px">
      This PS1 run should not be treated as a valid scene comparison run. The harness is now rejecting it instead of fabricating a scene timeline from title-contaminated or unrelated frames.
    </div>
  </div>
</section>
"""

    doc = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>{html.escape(args.title)}</title>
  <style>
    :root {{
      --bg:#0f1419;
      --panel:#182028;
      --panel2:#0d1117;
      --text:#e6edf3;
      --muted:#98a6b3;
      --border:#2f3b46;
      --good:#74d680;
      --bad:#ff7b72;
      --accent:#f2cc60;
    }}
    html,body {{ margin:0; background:var(--bg); color:var(--text); font:14px/1.45 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace; }}
    main {{ max-width: 1600px; margin:0 auto; padding:24px; }}
    .top {{
      position: sticky; top:0; z-index:10; background:rgba(15,20,25,0.95);
      backdrop-filter: blur(6px); border-bottom:1px solid var(--border); margin:-24px -24px 24px; padding:20px 24px 16px;
    }}
    h1,h2,p {{ margin:0; }}
    .summary {{ color:var(--muted); margin-top:8px; }}
    .summary-grid {{
      display:grid; grid-template-columns: repeat(8, minmax(0,1fr)); gap:10px; margin-top:16px;
    }}
    .box {{ background:var(--panel); border:1px solid var(--border); border-radius:10px; padding:12px; }}
    .box .k {{ color:var(--muted); font-size:12px; margin-bottom:6px; }}
    .box .v {{ font-size:18px; }}
    .lists {{ display:grid; grid-template-columns: 1fr 1fr; gap:16px; margin:16px 0 24px; }}
    .lists ul {{ margin:8px 0 0; max-height:180px; overflow:auto; }}
    .card {{ background:var(--panel); border:1px solid var(--border); border-radius:12px; padding:18px; margin-bottom:18px; }}
    .card-head {{ display:flex; justify-content:space-between; align-items:flex-start; gap:16px; margin-bottom:12px; }}
    .meta {{ color:var(--muted); margin-top:6px; }}
    .stats {{ display:flex; gap:8px; flex-wrap:wrap; }}
    .pill {{ border:1px solid var(--border); border-radius:999px; padding:6px 10px; color:var(--muted); }}
    .pill.good {{ color:var(--good); border-color:#2b5b34; }}
    .pill.bad {{ color:var(--bad); border-color:#6b2f2b; }}
    .grid {{ display:grid; grid-template-columns: 1fr 1fr; gap:16px; }}
    .timeline-grid {{ display:grid; grid-template-columns: repeat(2, minmax(0,1fr)); gap:16px; }}
    .timeline-figure {{ margin:0; background:var(--panel2); border:1px solid var(--border); border-radius:10px; overflow:hidden; }}
    figure {{ margin:0; background:var(--panel2); border:1px solid var(--border); border-radius:10px; overflow:hidden; }}
    figcaption {{ padding:10px 12px; border-bottom:1px solid var(--border); color:var(--accent); }}
    img {{ display:block; width:100%; height:auto; image-rendering: pixelated; background:#000; }}
    .path {{ padding:10px 12px; color:var(--muted); font-size:12px; word-break:break-all; }}
    code {{ color:var(--text); }}
    @media (max-width: 1000px) {{
      .summary-grid, .lists, .grid, .timeline-grid {{ grid-template-columns: 1fr; }}
      .card-head {{ flex-direction:column; }}
    }}
  </style>
</head>
<body>
  <main>
    <div class="top">
      <h1>{html.escape(args.title)}</h1>
      <p class="summary">Scrollable aligned timeline between the PS1 run and host run. This page shows every trusted aligned frame pair present in the compare JSON.</p>
      <div class="summary-grid">
        <div class="box"><div class="k">Alignment</div><div class="v"><code>{html.escape(str(data.get("alignment_mode")))}</code></div></div>
        <div class="box"><div class="k">Frame Offset</div><div class="v">{fmt_int(data.get("frame_offset"))}</div></div>
        <div class="box"><div class="k">PS1 Entry</div><div class="v">{fmt_int(data.get("result_entry_frame"))}</div></div>
        <div class="box"><div class="k">Host Entry</div><div class="v">{fmt_int(data.get("reference_entry_frame"))}</div></div>
        <div class="box"><div class="k">PS1 Min Scene Frame</div><div class="v">{fmt_int(data.get("min_result_scene_frame"))}</div></div>
        <div class="box"><div class="k">Host Min Scene Frame</div><div class="v">{fmt_int(data.get("min_reference_scene_frame"))}</div></div>
        <div class="box"><div class="k">Common Frames</div><div class="v">{fmt_int(data.get("common_frame_count"))}</div></div>
        <div class="box"><div class="k">Avg Diff</div><div class="v">{fmt_int(data.get("average_palette_index_diff_pixels"))}</div></div>
      </div>
    </div>

    <div class="lists">
      <div class="box">
        <div class="k">PS1-only frames in this run</div>
        <ul>{result_only}</ul>
      </div>
      <div class="box">
        <div class="k">Host-only frames in reference run</div>
        <ul>{ref_only}</ul>
      </div>
    </div>

    {cards_html}

    <section class="card">
      <div class="card-head">
        <div>
          <h2>Raw Timelines</h2>
          <div class="meta">These are the full original frame timelines for both runs. They are shown even when the harness cannot verify an aligned scene anchor.</div>
        </div>
      </div>
    </section>

    {render_frame_strip(result_run_frames, output_path, "PS1 Raw Timeline")}

    {render_frame_strip(reference_run_frames, output_path, "Host Raw Timeline")}
  </main>
</body>
</html>
"""

    output_path.write_text(doc)
    print(output_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
