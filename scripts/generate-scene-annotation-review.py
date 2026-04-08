#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
from typing import Any

import vision_vlm as vv


REVIEW_OPTIONS = [
    {"id": "correct", "label": "Correct"},
    {"id": "title_screen", "label": "Title screen"},
    {"id": "johnny_visible", "label": "Johnny visible"},
    {"id": "other_sprites_visible", "label": "Other sprites visible"},
    {"id": "screen_tearing_or_visual_issue", "label": "Screen tearing / visual issue"},
    {"id": "black_screen", "label": "Black screen"},
    {"id": "only_island", "label": "Only island"},
]


def rel(target: Path, base: Path) -> str:
    return os.path.relpath(target.resolve(), base.resolve())


def resolve_frames_dir(path: Path) -> Path:
    path = path.resolve()
    if path.is_dir():
        frames_dir = path / "frames"
        return frames_dir if frames_dir.is_dir() else path
    data = json.loads(path.read_text(encoding="utf-8"))
    frames_dir = data.get("paths", {}).get("frames_dir")
    if frames_dir:
        return Path(frames_dir).resolve()
    raise SystemExit(f"Unable to resolve frames dir from {path}")


def resolve_scene_id(path: Path) -> str | None:
    path = path.resolve()
    fallback = path.stem.upper()
    if path.is_file():
        data = json.loads(path.read_text(encoding="utf-8"))
        scene = data.get("scene", {})
        ads_name = scene.get("ads_name")
        tag = scene.get("tag")
        if ads_name and tag is not None:
            return f"{ads_name}-{tag}"
    return fallback


def sample_scene_frames(frames_dir: Path, *, prefer_scene_content: bool, skip_front_fraction: float = 0.0) -> list[Path]:
    frames = vv.collect_frame_paths(frames_dir)
    if prefer_scene_content:
        filtered = vv._filter_scene_content_frames(frames, yellow_threshold=1.5)
        if filtered:
            frames = filtered
    if skip_front_fraction > 0.0 and frames:
        skip = min(len(frames) - 1, int(len(frames) * skip_front_fraction))
        frames = frames[skip:]
    return frames


def build_frames(reference_frames: list[Path], query_frames: list[Path], *, paired: bool) -> list[dict[str, Any]]:
    if not query_frames:
        raise SystemExit("No query frames available for review")
    if paired:
        pair_count = min(len(reference_frames), len(query_frames))
        if pair_count <= 0:
            raise SystemExit("No frame pairs available for review")
        reference_sel: list[Path | None] = vv.vc.evenly_sample(reference_frames, pair_count)
        query_sel = vv.vc.evenly_sample(query_frames, pair_count)
    else:
        reference_sel = vv.vc.evenly_sample(reference_frames, len(query_frames)) if reference_frames else []
        query_sel = query_frames
    rows: list[dict[str, Any]] = []
    for index, query_image in enumerate(query_sel, start=1):
        reference_image = reference_sel[index - 1] if index - 1 < len(reference_sel) else None
        rows.append(
            {
                "id": f"pair_{index:03d}",
                "index": index,
                "reference_image": str(reference_image.resolve()) if reference_image else None,
                "query_image": str(query_image.resolve()),
                "reference_name": reference_image.name if reference_image else None,
                "query_name": query_image.name,
                "reference_frame": reference_image.stem if reference_image else None,
                "query_frame": query_image.stem,
                "labels": {},
                "johnny_capture": None,
                "notes": "",
            }
        )
    return rows


def build_html(title: str, scene_id: str, manifest_path: Path, annotations_path: Path, *, paired: bool) -> str:
    manifest_name = manifest_path.name
    annotations_name = annotations_path.name
    options_json = json.dumps(REVIEW_OPTIONS)
    pair_columns = "1fr 1fr" if paired else "1fr"
    reference_display = "block" if paired else "none"
    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>{title}</title>
  <style>
    :root {{
      --bg: #121419;
      --panel: #1b2028;
      --panel2: #0f1318;
      --text: #e8edf2;
      --muted: #96a3b2;
      --accent: #e7c35c;
      --line: #313a46;
      --good: #7bd389;
      --bad: #ff8b7a;
      --chip: #202833;
    }}
    * {{ box-sizing: border-box; }}
    body {{ margin: 0; background: var(--bg); color: var(--text); font: 14px/1.45 system-ui, sans-serif; }}
    .shell {{ max-width: 1600px; margin: 0 auto; padding: 20px; }}
    .top {{ display: flex; justify-content: space-between; gap: 16px; align-items: flex-start; margin-bottom: 16px; }}
    .title h1 {{ margin: 0 0 6px; font-size: 28px; }}
    .title p {{ margin: 0; color: var(--muted); max-width: 900px; }}
    .toolbar {{ display: flex; flex-wrap: wrap; gap: 8px; align-items: center; justify-content: flex-end; }}
    .toolbar button {{ background: var(--chip); color: var(--text); border: 1px solid var(--line); border-radius: 10px; padding: 10px 14px; cursor: pointer; }}
    .toolbar button:hover {{ border-color: var(--accent); }}
    .topnav {{ display: flex; gap: 8px; align-items: center; }}
    .status {{ color: var(--muted); font-size: 13px; min-width: 160px; text-align: right; }}
    .main {{ display: grid; grid-template-columns: 1fr 340px; gap: 16px; }}
    .viewer {{ background: var(--panel); border: 1px solid var(--line); border-radius: 16px; padding: 16px; }}
    .pairhead {{ display: flex; justify-content: space-between; gap: 12px; align-items: center; margin-bottom: 12px; }}
    .pairhead .meta {{ color: var(--muted); }}
    .pairgrid {{ display: grid; grid-template-columns: {pair_columns}; gap: 16px; }}
    figure {{ margin: 0; background: var(--panel2); border: 1px solid var(--line); border-radius: 12px; overflow: hidden; }}
    figcaption {{ padding: 10px 12px; border-bottom: 1px solid var(--line); color: var(--accent); font-weight: 600; }}
    img {{ display: block; width: auto; max-width: 100%; max-height: 34vh; height: auto; margin: 0 auto; image-rendering: pixelated; background: #000; }}
    .path {{ padding: 10px 12px 12px; font-family: ui-monospace, monospace; color: var(--muted); word-break: break-all; font-size: 12px; }}
    .controls {{ margin-top: 16px; display: grid; gap: 14px; }}
    .imagewrap {{ position: relative; }}
    .capture-marker {{
      position: absolute;
      width: 18px;
      height: 18px;
      margin-left: -9px;
      margin-top: -9px;
      border: 2px solid #ff6b6b;
      border-radius: 999px;
      box-shadow: 0 0 0 2px rgba(0,0,0,0.55);
      pointer-events: none;
      display: none;
    }}
    .capture-marker::before, .capture-marker::after {{
      content: "";
      position: absolute;
      background: #ff6b6b;
      left: 50%;
      top: 50%;
      transform: translate(-50%, -50%);
    }}
    .capture-marker::before {{ width: 2px; height: 24px; }}
    .capture-marker::after {{ width: 24px; height: 2px; }}
    .capturetools {{ display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }}
    .capturetools button {{
      background: var(--chip);
      color: var(--text);
      border: 1px solid var(--line);
      border-radius: 10px;
      padding: 10px 14px;
      cursor: pointer;
    }}
    .capturetools button.active {{ border-color: #ff6b6b; color: #ffb0b0; }}
    .capturemeta {{
      background: var(--panel2);
      border: 1px solid var(--line);
      border-radius: 12px;
      padding: 10px 12px;
      color: var(--muted);
      font-family: ui-monospace, monospace;
      font-size: 12px;
    }}
    .checkgrid {{ display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 10px; }}
    .check {{ background: var(--panel2); border: 1px solid var(--line); border-radius: 12px; padding: 10px 12px; display: flex; align-items: center; gap: 10px; }}
    .check input {{ width: 18px; height: 18px; }}
    textarea {{ width: 100%; min-height: 120px; resize: vertical; background: var(--panel2); color: var(--text); border: 1px solid var(--line); border-radius: 12px; padding: 12px; font: inherit; }}
    .side {{ background: var(--panel); border: 1px solid var(--line); border-radius: 16px; padding: 12px; display: flex; flex-direction: column; gap: 10px; max-height: calc(100vh - 40px); position: sticky; top: 20px; overflow: hidden; }}
    .summarybox {{ background: var(--panel2); border: 1px solid var(--line); border-radius: 12px; padding: 12px; color: var(--muted); }}
    .thumbs {{ overflow: auto; display: grid; gap: 8px; padding-right: 4px; }}
    .thumb {{ border: 1px solid var(--line); border-radius: 12px; padding: 10px; background: var(--panel2); cursor: pointer; text-align: left; }}
    .thumb.active {{ border-color: var(--accent); }}
    .thumb.done {{ border-color: var(--good); }}
    .thumb .name {{ margin-top: 6px; font-size: 12px; color: var(--muted); }}
    .thumb .title {{ font-size: 13px; color: var(--text); }}
    .chips {{ display: flex; flex-wrap: wrap; gap: 6px; margin-top: 6px; }}
    .chip {{ font-size: 11px; padding: 3px 8px; border-radius: 999px; background: var(--chip); border: 1px solid var(--line); color: var(--muted); }}
    .chip.on {{ color: var(--text); border-color: var(--accent); }}
    .navrow {{ display: flex; gap: 8px; }}
    .navrow button {{ flex: 1; }}
    @media (max-width: 1200px) {{
      .main {{ grid-template-columns: 1fr; }}
      .side {{ position: static; max-height: none; }}
      .pairgrid {{ grid-template-columns: 1fr; }}
    }}
  </style>
</head>
<body>
  <div class="shell">
    <div class="top">
      <div class="title">
        <h1>{title}</h1>
        <p>Scene: {scene_id}. Click through frames, tick the boxes that apply, and type notes when needed. Every change autosaves to a local JSON file.</p>
      </div>
      <div class="toolbar">
        <div class="topnav">
          <button id="prevBtn" type="button">Prev</button>
          <button id="nextBtn" type="button">Next</button>
        </div>
        <button id="exportBtn" type="button">Export JSON</button>
        <div class="status" id="saveStatus">Loading…</div>
      </div>
    </div>
    <div class="main">
      <div class="viewer">
        <div class="pairhead">
          <div>
            <div id="pairTitle">Frame</div>
            <div class="meta" id="pairMeta"></div>
          </div>
          <div class="meta" id="progressMeta"></div>
        </div>
        <div class="pairgrid">
          <figure>
            <figcaption>Query</figcaption>
            <div class="imagewrap">
              <img id="queryImage" alt="Query frame">
              <div class="capture-marker" id="johnnyMarker" aria-hidden="true"></div>
            </div>
            <div class="path" id="queryPath"></div>
          </figure>
        </div>
        <div class="controls">
          <div class="checkgrid" id="checkboxGrid"></div>
          <div class="capturetools">
            <button id="captureJohnnyBtn" type="button">Capture Johnny</button>
            <button id="clearJohnnyBtn" type="button">Clear Johnny</button>
            <div class="meta" id="captureHint">Click capture, then click Johnny in the query image.</div>
          </div>
          <div class="capturemeta" id="johnnyMeta">Johnny capture: none</div>
          <textarea id="notesBox" placeholder="Notes, discrepancies, what is actually visible, etc."></textarea>
        </div>
      </div>
      <aside class="side">
        <div class="summarybox" id="summaryBox"></div>
        <div class="thumbs" id="thumbList"></div>
      </aside>
    </div>
  </div>
  <script>
    const reviewOptions = {options_json};
    const manifestUrl = {json.dumps(manifest_name)};
    const annotationsUrl = {json.dumps(annotations_name)};
    const saveUrl = '/api/save';
    let manifest = null;
    let currentIndex = 0;
    let saveTimer = null;
    let captureJohnnyMode = false;

    function qs(id) {{
      return document.getElementById(id);
    }}

    function deepClone(value) {{
      return JSON.parse(JSON.stringify(value));
    }}

    async function loadJson(url) {{
      const res = await fetch(url, {{ cache: 'no-store' }});
      if (!res.ok) {{
        throw new Error(`Failed to load ${{url}}: ${{res.status}}`);
      }}
      return await res.json();
    }}

    async function boot() {{
      manifest = await loadJson(manifestUrl);
      try {{
        const saved = await loadJson('/api/annotations');
        if (saved && Array.isArray(saved.frames)) {{
          const byId = new Map(saved.frames.map((frame) => [frame.id, frame]));
          for (const frame of manifest.frames) {{
            const prior = byId.get(frame.id);
            if (prior) {{
              frame.labels = prior.labels || {{}};
              frame.johnny_capture = prior.johnny_capture || null;
              frame.notes = prior.notes || '';
            }}
          }}
        }}
      }} catch (err) {{
      }}
      buildCheckboxes();
      buildThumbs();
      render();
      setStatus('Ready');
    }}

    function buildCheckboxes() {{
      const grid = qs('checkboxGrid');
      grid.innerHTML = '';
      for (const opt of reviewOptions) {{
        const wrap = document.createElement('label');
        wrap.className = 'check';
        const box = document.createElement('input');
        box.type = 'checkbox';
        box.dataset.optionId = opt.id;
        box.addEventListener('change', () => {{
          const frame = manifest.frames[currentIndex];
          frame.labels[opt.id] = box.checked;
          queueSave();
          updateThumb(currentIndex);
          updateSummary();
        }});
        const text = document.createElement('span');
        text.textContent = opt.label;
        wrap.appendChild(box);
        wrap.appendChild(text);
        grid.appendChild(wrap);
      }}
    }}

    function buildThumbs() {{
      const thumbList = qs('thumbList');
      thumbList.innerHTML = '';
      manifest.frames.forEach((frame, index) => {{
        const item = document.createElement('button');
        item.type = 'button';
        item.className = 'thumb';
        item.id = `thumb_${{index}}`;
        item.addEventListener('click', () => {{
          currentIndex = index;
          render();
        }});
        const title = document.createElement('div');
        title.className = 'title';
        title.textContent = `${{index + 1}}. ${{frame.query_frame}}`;
        const name = document.createElement('div');
        name.className = 'name';
        name.textContent = frame.query_name;
        const chips = document.createElement('div');
        chips.className = 'chips';
        item.appendChild(title);
        item.appendChild(name);
        item.appendChild(chips);
        thumbList.appendChild(item);
      }});
    }}

    function labeledCount(frame) {{
      return reviewOptions.filter((opt) => frame.labels && frame.labels[opt.id]).length + (frame.notes || '').trim().length > 0;
    }}

    function frameIsDone(frame) {{
      return reviewOptions.some((opt) => frame.labels && frame.labels[opt.id]) ||
             Boolean(frame.johnny_capture) ||
             ((frame.notes || '').trim().length > 0);
    }}

    function updateThumb(index) {{
      const frame = manifest.frames[index];
      const thumb = qs(`thumb_${{index}}`);
      if (!thumb) return;
      thumb.classList.toggle('active', index === currentIndex);
      thumb.classList.toggle('done', frameIsDone(frame));
      const chips = thumb.querySelector('.chips');
      chips.innerHTML = '';
      for (const opt of reviewOptions) {{
        if (!frame.labels || !frame.labels[opt.id]) continue;
        const chip = document.createElement('span');
        chip.className = 'chip on';
        chip.textContent = opt.label;
        chips.appendChild(chip);
      }}
      if (frame.johnny_capture) {{
        const chip = document.createElement('span');
        chip.className = 'chip on';
        chip.textContent = `Johnny @ ${{Math.round(frame.johnny_capture.x_px)}},${{Math.round(frame.johnny_capture.y_px)}}`;
        chips.appendChild(chip);
      }}
    }}

    function updateAllThumbs() {{
      manifest.frames.forEach((_, index) => updateThumb(index));
    }}

    function updateSummary() {{
      const total = manifest.frames.length;
      const done = manifest.frames.filter(frameIsDone).length;
      const counts = Object.fromEntries(reviewOptions.map((opt) => [opt.id, 0]));
      let johnnyMarked = 0;
      for (const frame of manifest.frames) {{
        for (const opt of reviewOptions) {{
          if (frame.labels && frame.labels[opt.id]) counts[opt.id] += 1;
        }}
        if (frame.johnny_capture) johnnyMarked += 1;
      }}
      const lines = [
        `<div><strong>${{done}} / ${{total}}</strong> frames labeled</div>`,
      ];
      for (const opt of reviewOptions) {{
        lines.push(`<div>${{opt.label}}: ${{counts[opt.id]}}</div>`);
      }}
      lines.push(`<div>Johnny captured: ${{johnnyMarked}}</div>`);
      qs('summaryBox').innerHTML = lines.join('');
    }}

    function renderJohnnyCapture(frame) {{
      const marker = qs('johnnyMarker');
      const meta = qs('johnnyMeta');
      if (frame.johnny_capture) {{
        marker.style.display = 'block';
        marker.style.left = `${{frame.johnny_capture.x_pct}}%`;
        marker.style.top = `${{frame.johnny_capture.y_pct}}%`;
        meta.textContent = `Johnny capture: x=${{Math.round(frame.johnny_capture.x_px)}} y=${{Math.round(frame.johnny_capture.y_px)}} (${{Math.round(frame.johnny_capture.x_pct * 10) / 10}}%, ${{Math.round(frame.johnny_capture.y_pct * 10) / 10}}%)`;
      }} else {{
        marker.style.display = 'none';
        meta.textContent = 'Johnny capture: none';
      }}
    }}

    function setCaptureJohnnyMode(active) {{
      captureJohnnyMode = active;
      qs('captureJohnnyBtn').classList.toggle('active', active);
      qs('captureHint').textContent = active
        ? 'Capture mode active: click Johnny in the query image.'
        : 'Click capture, then click Johnny in the query image.';
    }}

    function render() {{
      const frame = manifest.frames[currentIndex];
      qs('pairTitle').textContent = `${{manifest.scene_id}} · frame ${{currentIndex + 1}}`;
      qs('pairMeta').textContent = `${{frame.query_frame}}`;
      qs('progressMeta').textContent = `${{currentIndex + 1}} / ${{manifest.frames.length}}`;
      qs('queryImage').src = frame.query_rel;
      qs('queryPath').textContent = frame.query_image;
      for (const box of qs('checkboxGrid').querySelectorAll('input[type=checkbox]')) {{
        box.checked = Boolean(frame.labels && frame.labels[box.dataset.optionId]);
      }}
      qs('notesBox').value = frame.notes || '';
      renderJohnnyCapture(frame);
      updateAllThumbs();
      updateSummary();
    }}

    async function saveNow() {{
      if (!manifest) return;
      const payload = deepClone({{
        scene_id: manifest.scene_id,
        result: manifest.result,
        reference: manifest.reference,
        frames: manifest.frames.map((frame) => ({{
          id: frame.id,
          reference_image: frame.reference_image,
          query_image: frame.query_image,
          labels: frame.labels || {{}},
          johnny_capture: frame.johnny_capture || null,
          notes: frame.notes || '',
        }})),
      }});
      setStatus('Saving…');
      const res = await fetch(saveUrl, {{
        method: 'POST',
        headers: {{ 'Content-Type': 'application/json' }},
        body: JSON.stringify(payload, null, 2),
      }});
      if (!res.ok) {{
        setStatus('Save failed');
        return;
      }}
      const body = await res.json();
      setStatus(`Saved ${{body.saved_count}} frames`);
    }}

    function queueSave() {{
      if (saveTimer) window.clearTimeout(saveTimer);
      saveTimer = window.setTimeout(() => {{
        saveTimer = null;
        saveNow();
      }}, 150);
    }}

    function setStatus(text) {{
      qs('saveStatus').textContent = text;
    }}

    function move(delta) {{
      currentIndex = Math.max(0, Math.min(manifest.frames.length - 1, currentIndex + delta));
      render();
    }}

    qs('notesBox').addEventListener('input', (event) => {{
      if (!manifest) return;
      manifest.frames[currentIndex].notes = event.target.value;
      queueSave();
      updateThumb(currentIndex);
      updateSummary();
    }});
    qs('captureJohnnyBtn').addEventListener('click', () => {{
      setCaptureJohnnyMode(!captureJohnnyMode);
    }});
    qs('clearJohnnyBtn').addEventListener('click', () => {{
      if (!manifest) return;
      manifest.frames[currentIndex].johnny_capture = null;
      setCaptureJohnnyMode(false);
      renderJohnnyCapture(manifest.frames[currentIndex]);
      queueSave();
      updateThumb(currentIndex);
      updateSummary();
    }});
    qs('queryImage').addEventListener('click', (event) => {{
      if (!manifest || !captureJohnnyMode) return;
      const frame = manifest.frames[currentIndex];
      const rect = event.target.getBoundingClientRect();
      const naturalWidth = event.target.naturalWidth || rect.width;
      const naturalHeight = event.target.naturalHeight || rect.height;
      const xPct = ((event.clientX - rect.left) / rect.width) * 100;
      const yPct = ((event.clientY - rect.top) / rect.height) * 100;
      frame.johnny_capture = {{
        x_pct: Math.max(0, Math.min(100, xPct)),
        y_pct: Math.max(0, Math.min(100, yPct)),
        x_px: ((event.clientX - rect.left) / rect.width) * naturalWidth,
        y_px: ((event.clientY - rect.top) / rect.height) * naturalHeight,
      }};
      frame.labels = frame.labels || {{}};
      frame.labels.johnny_visible = true;
      setCaptureJohnnyMode(false);
      renderJohnnyCapture(frame);
      queueSave();
      updateThumb(currentIndex);
      updateSummary();
      render();
    }});
    qs('prevBtn').addEventListener('click', (event) => {{ event.preventDefault(); move(-1); }});
    qs('nextBtn').addEventListener('click', (event) => {{ event.preventDefault(); move(1); }});
    qs('exportBtn').addEventListener('click', () => {{
      const blob = new Blob([JSON.stringify({{
        scene_id: manifest.scene_id,
        frames: manifest.frames.map((frame) => ({{
          id: frame.id,
          reference_image: frame.reference_image,
          query_image: frame.query_image,
          labels: frame.labels || {{}},
          johnny_capture: frame.johnny_capture || null,
          notes: frame.notes || '',
        }})),
      }}, null, 2)], {{ type: 'application/json' }});
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = '{scene_id.lower()}-annotations.json';
      a.click();
      URL.revokeObjectURL(url);
    }});

    window.addEventListener('keydown', (event) => {{
      if (!manifest) return;
      if (event.target && event.target.tagName === 'TEXTAREA') return;
      if (event.key === 'ArrowLeft') move(-1);
      if (event.key === 'ArrowRight') move(1);
    }});

    boot().catch((err) => {{
      console.error(err);
      setStatus(String(err));
    }});
  </script>
</body>
</html>
"""


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate a one-scene annotation review UI.")
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--result", type=Path, required=True)
    parser.add_argument("--outdir", type=Path, required=True)
    parser.add_argument("--scene-id", type=str)
    parser.add_argument("--title", type=str, default="PS1 Scene Annotation Review")
    parser.add_argument("--prefer-scene-content", action="store_true", default=True)
    parser.add_argument("--all-frames", action="store_true")
    parser.add_argument("--paired", action="store_true", help="Review reference/query pairs instead of single query frames")
    args = parser.parse_args()

    outdir = args.outdir.resolve()
    outdir.mkdir(parents=True, exist_ok=True)
    scene_id = args.scene_id or resolve_scene_id(args.reference) or resolve_scene_id(args.result)
    reference_frames_dir = resolve_frames_dir(args.reference)
    query_frames_dir = resolve_frames_dir(args.result)

    if args.all_frames:
        reference_frames = vv.collect_frame_paths(reference_frames_dir)
        query_frames = vv.collect_frame_paths(query_frames_dir)
    else:
        reference_frames = sample_scene_frames(reference_frames_dir, prefer_scene_content=True, skip_front_fraction=0.2)
        query_frames = sample_scene_frames(query_frames_dir, prefer_scene_content=True)
        if not reference_frames:
            reference_frames = vv.collect_frame_paths(reference_frames_dir)
        if not query_frames:
            query_frames = vv.collect_frame_paths(query_frames_dir)

    frames = build_frames(reference_frames, query_frames, paired=args.paired)
    manifest = {
        "scene_id": scene_id,
        "title": args.title,
        "mode": "paired" if args.paired else "single_frame",
        "reference": str(args.reference.resolve()),
        "result": str(args.result.resolve()),
        "frames": [],
    }
    for frame in frames:
        manifest["frames"].append(
            {
                **frame,
                "reference_rel": rel(Path(frame["reference_image"]), outdir) if frame["reference_image"] else None,
                "query_rel": rel(Path(frame["query_image"]), outdir),
            }
        )

    manifest_path = outdir / "manifest.json"
    annotations_path = outdir / "annotations.json"
    html_path = outdir / "review.html"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    if not annotations_path.exists():
        annotations_path.write_text(
            json.dumps(
                {
                    "scene_id": scene_id,
                    "result": str(args.result.resolve()),
                    "reference": str(args.reference.resolve()),
                    "frames": [],
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
    html_path.write_text(build_html(args.title, scene_id, manifest_path, annotations_path, paired=args.paired), encoding="utf-8")
    print(html_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
