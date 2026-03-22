#!/usr/bin/env python3
"""analyze-regtest.py — Analyze duckstation-regtest output for a single scene.

Reads frame PNGs, runs decode-ps1-bars.py on them, inspects printf logs,
compares against baselines, detects regressions, and generates an HTML report.

Usage:
  python3 scripts/analyze-regtest.py results/stand-2/
  python3 scripts/analyze-regtest.py results/stand-2/ --baseline baselines/stand-2/
  python3 scripts/analyze-regtest.py results/stand-2/ --html report.html
  python3 scripts/analyze-regtest.py regtest-results/run-*/  --summary

Returns nonzero if any regression is detected.
"""

from __future__ import annotations

import argparse
import glob
import hashlib
import json
import os
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple


# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------
@dataclass
class FrameAnalysis:
    path: str
    index: int
    sha256: str
    is_mostly_black: bool
    telemetry: Optional[Dict[str, Any]] = None
    drop_thread_drops: int = 0
    drop_bmp_frame_cap: int = 0
    drop_bmp_short_load: int = 0


@dataclass
class SceneResult:
    scene_dir: str
    ads_name: str = ""
    tag: int = 0
    frames: List[FrameAnalysis] = field(default_factory=list)
    printf_output: str = ""
    has_fatal_error: bool = False
    crash_lines: List[str] = field(default_factory=list)
    regressions: List[str] = field(default_factory=list)
    baseline_diffs: List[str] = field(default_factory=list)
    exit_code: int = 0
    timed_out: bool = False


# ---------------------------------------------------------------------------
# Frame analysis helpers
# ---------------------------------------------------------------------------
CRASH_PATTERNS = [
    "fatalError",
    "FATAL",
    "panic",
    "assert",
    "ASSERT",
    "abort",
    "crash",
    "CRASH",
    "Exception",
    "Segmentation",
    "SIGSEGV",
    "bus error",
]

BLACK_THRESHOLD = 0.92  # If >92% of pixels are near-black, it's a black frame


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


def is_mostly_black(path: str, threshold: float = BLACK_THRESHOLD) -> bool:
    """Check if a frame image is predominantly black (crash/hang indicator)."""
    try:
        from PIL import Image
    except ImportError:
        return False

    try:
        img = Image.open(path).convert("RGB")
        px = img.load()
        total = img.width * img.height
        black_count = 0
        # Sample every 4th pixel for speed
        step = 4
        sampled = 0
        for y in range(0, img.height, step):
            for x in range(0, img.width, step):
                r, g, b = px[x, y]
                if max(r, g, b) < 16:
                    black_count += 1
                sampled += 1
        return (black_count / max(sampled, 1)) > threshold
    except Exception:
        return False


def run_telemetry_decode(
    frame_paths: List[str], project_root: str
) -> Dict[str, Any]:
    """Run decode-ps1-bars.py on frame images and return parsed JSON."""
    decode_script = os.path.join(project_root, "scripts", "decode-ps1-bars.py")
    if not os.path.isfile(decode_script):
        return {}

    try:
        result = subprocess.run(
            [sys.executable, decode_script, "--json", "--include-zero"] + frame_paths,
            capture_output=True,
            text=True,
            timeout=60,
        )
        if result.returncode == 0 and result.stdout.strip():
            data = json.loads(result.stdout)
            if isinstance(data, dict):
                return {frame_paths[0]: data}
            elif isinstance(data, list):
                mapping = {}
                for i, entry in enumerate(data):
                    if i < len(frame_paths):
                        mapping[frame_paths[i]] = entry
                return mapping
    except (subprocess.TimeoutExpired, json.JSONDecodeError, Exception):
        pass
    return {}


def extract_drop_values(telemetry: Dict[str, Any]) -> Tuple[int, int, int]:
    """Extract drop bar values from telemetry data."""
    rows = telemetry.get("rows", [])
    drops = {r["key"]: r.get("width", 0) for r in rows if r.get("key", "").startswith("drop_")}
    return (
        drops.get("drop_thread_drops", 0),
        drops.get("drop_bmp_frame_cap", 0),
        drops.get("drop_bmp_short_load", 0),
    )


# ---------------------------------------------------------------------------
# Analysis pipeline
# ---------------------------------------------------------------------------
def analyze_scene(
    scene_dir: str,
    baseline_dir: Optional[str],
    project_root: str,
) -> SceneResult:
    """Full analysis of a single scene's regtest output."""
    result = SceneResult(scene_dir=os.path.abspath(scene_dir))

    # Read result.json if it exists
    result_json_path = os.path.join(scene_dir, "result.json")
    if os.path.isfile(result_json_path):
        try:
            with open(result_json_path) as f:
                rj = json.load(f)
            scene_info = rj.get("scene", {})
            result.ads_name = scene_info.get("ads_name", "")
            result.tag = scene_info.get("tag", 0)
            outcome = rj.get("outcome", {})
            result.exit_code = outcome.get("exit_code", 0)
            result.timed_out = outcome.get("timed_out", False)
            result.has_fatal_error = outcome.get("has_fatal_error", False)
        except (json.JSONDecodeError, KeyError):
            pass

    # Read printf log
    printf_path = os.path.join(scene_dir, "printf.log")
    if os.path.isfile(printf_path):
        with open(printf_path) as f:
            result.printf_output = f.read()
        for line in result.printf_output.splitlines():
            for pat in CRASH_PATTERNS:
                if pat in line:
                    result.crash_lines.append(line.strip())
                    break

    if result.crash_lines:
        result.has_fatal_error = True

    # Find frame images
    frames_dir = os.path.join(scene_dir, "frames")
    frame_paths: List[str] = []
    if os.path.isdir(frames_dir):
        frame_paths = sorted(glob.glob(os.path.join(frames_dir, "*.png")))

    if not frame_paths:
        result.regressions.append("no_frames_captured")
        return result

    # Run telemetry decode in batch (more efficient)
    telemetry_map = run_telemetry_decode(frame_paths, project_root)

    # Analyze each frame
    for i, fpath in enumerate(frame_paths):
        fhash = sha256_file(fpath)
        black = is_mostly_black(fpath)
        telem = telemetry_map.get(fpath)

        thread_drops = 0
        bmp_frame_cap = 0
        bmp_short_load = 0
        if telem:
            thread_drops, bmp_frame_cap, bmp_short_load = extract_drop_values(telem)

        fa = FrameAnalysis(
            path=fpath,
            index=i,
            sha256=fhash,
            is_mostly_black=black,
            telemetry=telem,
            drop_thread_drops=thread_drops,
            drop_bmp_frame_cap=bmp_frame_cap,
            drop_bmp_short_load=bmp_short_load,
        )
        result.frames.append(fa)

    # --- Regression detection ---
    # 1. Black frames (crash/hang)
    black_frames = [f for f in result.frames if f.is_mostly_black]
    if black_frames:
        indices = [str(f.index) for f in black_frames]
        result.regressions.append(f"black_frames: indices {','.join(indices)}")

    # 2. Thread drops > 0
    drop_frames = [f for f in result.frames if f.drop_thread_drops > 0]
    if drop_frames:
        max_drops = max(f.drop_thread_drops for f in drop_frames)
        result.regressions.append(
            f"thread_drops: {len(drop_frames)} frames with drops (max={max_drops})"
        )

    # 3. Fatal error in printf
    if result.has_fatal_error:
        result.regressions.append(
            f"fatal_error: {len(result.crash_lines)} crash indicator(s) in printf log"
        )

    # 4. Timeout
    if result.timed_out:
        result.regressions.append("timeout: scene did not complete in time")

    # --- Baseline comparison ---
    if baseline_dir and os.path.isdir(baseline_dir):
        baseline_frames_dir = os.path.join(baseline_dir, "frames")
        if os.path.isdir(baseline_frames_dir):
            baseline_frames = sorted(glob.glob(os.path.join(baseline_frames_dir, "*.png")))
            for bf in baseline_frames:
                bf_name = os.path.basename(bf)
                bf_hash = sha256_file(bf)
                # Find corresponding frame in current results
                match = None
                for cf in result.frames:
                    if os.path.basename(cf.path) == bf_name:
                        match = cf
                        break
                if match is None:
                    result.baseline_diffs.append(
                        f"missing_frame: {bf_name} (in baseline but not in current)"
                    )
                elif match.sha256 != bf_hash:
                    result.baseline_diffs.append(
                        f"hash_diff: {bf_name} baseline={bf_hash[:12]}... current={match.sha256[:12]}..."
                    )

        # Check baseline result.json for hash comparison
        baseline_result_path = os.path.join(baseline_dir, "result.json")
        if os.path.isfile(baseline_result_path):
            try:
                with open(baseline_result_path) as f:
                    br = json.load(f)
                baseline_hash = br.get("outcome", {}).get("state_hash")
                current_hash = result.frames[-1].sha256 if result.frames else None
                if baseline_hash and current_hash and baseline_hash != current_hash:
                    result.baseline_diffs.append(
                        f"state_hash_diff: baseline={baseline_hash[:16]}... current={current_hash[:16]}..."
                    )
            except (json.JSONDecodeError, KeyError):
                pass

    return result


# ---------------------------------------------------------------------------
# HTML report generation
# ---------------------------------------------------------------------------
def generate_html_report(results: List[SceneResult], output_path: str) -> None:
    """Generate an HTML report with thumbnails and telemetry summaries."""
    html_parts = [
        "<!DOCTYPE html>",
        "<html><head>",
        "<meta charset='utf-8'>",
        "<title>PS1 Regtest Report</title>",
        "<style>",
        "body { font-family: monospace; background: #1a1a1a; color: #e0e0e0; margin: 20px; }",
        "h1 { color: #4fc3f7; }",
        "h2 { color: #81c784; border-bottom: 1px solid #444; padding-bottom: 4px; }",
        "table { border-collapse: collapse; margin: 10px 0; }",
        "th, td { border: 1px solid #555; padding: 4px 8px; text-align: left; }",
        "th { background: #333; }",
        ".pass { color: #81c784; font-weight: bold; }",
        ".fail { color: #e57373; font-weight: bold; }",
        ".warn { color: #ffb74d; }",
        ".regression { background: #3e2723; padding: 4px 8px; margin: 2px 0; border-left: 3px solid #e57373; }",
        ".baseline-diff { background: #1a237e; padding: 4px 8px; margin: 2px 0; border-left: 3px solid #42a5f5; }",
        ".thumb-row { display: flex; flex-wrap: wrap; gap: 8px; margin: 8px 0; }",
        ".thumb { max-width: 200px; border: 1px solid #555; }",
        ".thumb-label { font-size: 10px; color: #aaa; }",
        ".scene-block { margin: 16px 0; padding: 12px; border: 1px solid #444; border-radius: 4px; }",
        "</style>",
        "</head><body>",
        "<h1>PS1 Regtest Report</h1>",
    ]

    # Summary table
    total = len(results)
    passed = sum(1 for r in results if not r.regressions)
    failed = total - passed
    html_parts.append(f"<p>Total: {total} | <span class='pass'>Passed: {passed}</span> | <span class='fail'>Failed: {failed}</span></p>")

    html_parts.append("<table>")
    html_parts.append("<tr><th>Scene</th><th>Frames</th><th>Status</th><th>Regressions</th><th>Baseline Diffs</th></tr>")

    for r in results:
        status_class = "fail" if r.regressions else "pass"
        status_text = "FAIL" if r.regressions else "PASS"
        if r.timed_out:
            status_text = "TIMEOUT"
        reg_count = len(r.regressions)
        diff_count = len(r.baseline_diffs)
        html_parts.append(
            f"<tr>"
            f"<td>{r.ads_name} {r.tag}</td>"
            f"<td>{len(r.frames)}</td>"
            f"<td class='{status_class}'>{status_text}</td>"
            f"<td>{reg_count}</td>"
            f"<td>{diff_count}</td>"
            f"</tr>"
        )

    html_parts.append("</table>")

    # Per-scene details
    for r in results:
        scene_label = f"{r.ads_name} {r.tag}" if r.ads_name else os.path.basename(r.scene_dir)
        status_class = "fail" if r.regressions else "pass"
        status_text = "FAIL" if r.regressions else "PASS"

        html_parts.append(f"<div class='scene-block'>")
        html_parts.append(f"<h2>{scene_label} <span class='{status_class}'>[{status_text}]</span></h2>")

        # Regressions
        if r.regressions:
            html_parts.append("<h3 class='fail'>Regressions</h3>")
            for reg in r.regressions:
                html_parts.append(f"<div class='regression'>{reg}</div>")

        # Baseline diffs
        if r.baseline_diffs:
            html_parts.append("<h3 class='warn'>Baseline Diffs</h3>")
            for diff in r.baseline_diffs:
                html_parts.append(f"<div class='baseline-diff'>{diff}</div>")

        # Frame thumbnails
        if r.frames:
            html_parts.append("<h3>Frames</h3>")
            html_parts.append("<div class='thumb-row'>")
            for fa in r.frames:
                rel_path = os.path.relpath(fa.path, os.path.dirname(output_path))
                black_marker = " [BLACK]" if fa.is_mostly_black else ""
                drops_marker = f" drops={fa.drop_thread_drops}" if fa.drop_thread_drops > 0 else ""
                html_parts.append(
                    f"<div>"
                    f"<img class='thumb' src='{rel_path}' alt='frame {fa.index}'>"
                    f"<div class='thumb-label'>#{fa.index}{black_marker}{drops_marker}<br>{fa.sha256[:12]}...</div>"
                    f"</div>"
                )
            html_parts.append("</div>")

        # Telemetry table for last frame
        if r.frames and r.frames[-1].telemetry:
            telem = r.frames[-1].telemetry
            rows = telem.get("rows", [])
            if rows:
                html_parts.append("<h3>Telemetry (last frame)</h3>")
                html_parts.append("<table><tr><th>Key</th><th>Width</th><th>Description</th></tr>")
                for row in rows:
                    w = row.get("width", 0)
                    style = " class='warn'" if w > 0 and row.get("key", "").startswith("drop_") else ""
                    html_parts.append(
                        f"<tr{style}><td>{row.get('key', '')}</td>"
                        f"<td>{w}</td>"
                        f"<td>{row.get('desc', '')}</td></tr>"
                    )
                html_parts.append("</table>")

        # Crash lines
        if r.crash_lines:
            html_parts.append("<h3 class='fail'>Crash Indicators</h3>")
            html_parts.append("<pre>")
            for line in r.crash_lines[:20]:
                html_parts.append(line)
            html_parts.append("</pre>")

        html_parts.append("</div>")

    html_parts.append("</body></html>")

    with open(output_path, "w") as f:
        f.write("\n".join(html_parts))


# ---------------------------------------------------------------------------
# JSON output
# ---------------------------------------------------------------------------
def result_to_dict(r: SceneResult) -> Dict[str, Any]:
    return {
        "scene_dir": r.scene_dir,
        "ads_name": r.ads_name,
        "tag": r.tag,
        "frame_count": len(r.frames),
        "exit_code": r.exit_code,
        "timed_out": r.timed_out,
        "has_fatal_error": r.has_fatal_error,
        "regressions": r.regressions,
        "baseline_diffs": r.baseline_diffs,
        "crash_lines": r.crash_lines[:20],
        "frames": [
            {
                "index": f.index,
                "sha256": f.sha256,
                "is_mostly_black": f.is_mostly_black,
                "drop_thread_drops": f.drop_thread_drops,
                "drop_bmp_frame_cap": f.drop_bmp_frame_cap,
                "drop_bmp_short_load": f.drop_bmp_short_load,
            }
            for f in r.frames
        ],
    }


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main() -> int:
    ap = argparse.ArgumentParser(
        description="Analyze PS1 regtest output and detect regressions."
    )
    ap.add_argument(
        "dirs",
        nargs="+",
        help="Scene result directory (or multiple for --summary mode)",
    )
    ap.add_argument(
        "--baseline",
        default=None,
        help="Baseline directory for comparison (single-scene mode)",
    )
    ap.add_argument(
        "--html",
        default=None,
        help="Generate HTML report at this path",
    )
    ap.add_argument(
        "--json",
        action="store_true",
        help="Output JSON to stdout",
    )
    ap.add_argument(
        "--summary",
        action="store_true",
        help="Analyze multiple scene directories and produce combined report",
    )
    ap.add_argument(
        "--project-root",
        default=None,
        help="Project root directory (default: auto-detect from script location)",
    )
    args = ap.parse_args()

    # Detect project root
    project_root = args.project_root
    if project_root is None:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        project_root = os.path.dirname(script_dir)

    # Analyze each directory
    results: List[SceneResult] = []
    for d in args.dirs:
        # Expand globs (shell may not expand them)
        expanded = glob.glob(d) if "*" in d or "?" in d else [d]
        for ed in expanded:
            if not os.path.isdir(ed):
                print(f"WARNING: Not a directory, skipping: {ed}", file=sys.stderr)
                continue
            baseline = args.baseline if not args.summary else None
            r = analyze_scene(ed, baseline, project_root)
            results.append(r)

    if not results:
        print("ERROR: No valid scene directories found.", file=sys.stderr)
        return 1

    # Output
    has_regressions = any(r.regressions for r in results)

    if args.json:
        if len(results) == 1 and not args.summary:
            print(json.dumps(result_to_dict(results[0]), indent=2))
        else:
            output = {
                "total": len(results),
                "passed": sum(1 for r in results if not r.regressions),
                "failed": sum(1 for r in results if r.regressions),
                "scenes": [result_to_dict(r) for r in results],
            }
            print(json.dumps(output, indent=2))
    else:
        # Human-readable output
        for r in results:
            label = f"{r.ads_name} {r.tag}" if r.ads_name else os.path.basename(r.scene_dir)
            status = "FAIL" if r.regressions else "PASS"
            print(f"=== {label} [{status}] ===")
            print(f"  Frames: {len(r.frames)}")
            print(f"  Exit code: {r.exit_code}")
            if r.timed_out:
                print("  TIMED OUT")
            if r.regressions:
                print("  Regressions:")
                for reg in r.regressions:
                    print(f"    - {reg}")
            if r.baseline_diffs:
                print("  Baseline diffs:")
                for diff in r.baseline_diffs:
                    print(f"    - {diff}")
            if r.crash_lines:
                print(f"  Crash indicators ({len(r.crash_lines)}):")
                for line in r.crash_lines[:5]:
                    print(f"    | {line}")
            print()

    # Generate HTML report
    html_path = args.html
    if html_path is None and args.summary and len(args.dirs) == 1:
        # Auto-generate in run directory
        run_dir = args.dirs[0].rstrip("/")
        if os.path.isdir(run_dir):
            html_path = os.path.join(run_dir, "report.html")
    if html_path:
        generate_html_report(results, html_path)
        print(f"HTML report: {html_path}", file=sys.stderr)

    return 1 if has_regressions else 0


if __name__ == "__main__":
    raise SystemExit(main())
