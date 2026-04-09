#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJECT_ROOT="/home/hunter/workspace/jc_reborn"
REFDIR="$PROJECT_ROOT/regtest-references"
OUTROOT_DEFAULT="$PROJECT_ROOT/vision-artifacts/vision-reference-pipeline-$(date +%Y%m%d-%H%M%S)"
OUTROOT="${1:-$OUTROOT_DEFAULT}"
BANKDIR="$OUTROOT/reference-bank"
SELFCHECKDIR="$OUTROOT/reference-selfcheck"
MANIFEST="$OUTROOT/pipeline-manifest.json"
MANIFEST_HTML="$OUTROOT/index.html"

mkdir -p "$OUTROOT"

echo "Building reference bank into $BANKDIR"
python3 "$ROOT/scripts/vision_classifier.py" \
  build-reference-bank \
  --refdir "$REFDIR" \
  --outdir "$BANKDIR"

echo "Running reference self-check into $SELFCHECKDIR"
python3 "$ROOT/scripts/vision_classifier.py" \
  analyze-reference-set \
  --refdir "$REFDIR" \
  --bank-dir "$BANKDIR" \
  --outdir "$SELFCHECKDIR"

python3 - <<'PY' "$BANKDIR" "$SELFCHECKDIR" "$MANIFEST" "$MANIFEST_HTML"
from pathlib import Path
import json
import sys
import os

bankdir = Path(sys.argv[1])
selfcheckdir = Path(sys.argv[2])
manifest_path = Path(sys.argv[3])
html_path = Path(sys.argv[4])
outroot = manifest_path.parent

def rel_to(root: Path, target: Path) -> str:
    try:
        return target.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return target.resolve().as_posix()

def rel_href(target: Path) -> str:
    return os.path.relpath(target.resolve(), html_path.parent.resolve())

bank = json.loads((bankdir / "index.json").read_text())
selfcheck = json.loads((selfcheckdir / "index.json").read_text())
quality = json.loads((selfcheckdir / "quality-report.json").read_text())

manifest = {
    "reference_bank": {
        "index_html": rel_to(outroot, bankdir / "index.html"),
        "index_json": rel_to(outroot, bankdir / "index.json"),
        "metadata_json": rel_to(outroot, bankdir / "metadata.json"),
        "features_npy": rel_to(outroot, bankdir / "features.npy"),
        "scene_count": len(bank["scenes"]),
        "frame_count": bank["frame_count"],
    },
    "reference_selfcheck": {
        "index_html": rel_to(outroot, selfcheckdir / "index.html"),
        "index_json": rel_to(outroot, selfcheckdir / "index.json"),
        "quality_html": rel_to(outroot, selfcheckdir / "quality-report.html"),
        "quality_json": rel_to(outroot, selfcheckdir / "quality-report.json"),
        "scene_count": selfcheck["scene_count"],
    },
}
manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

best = sorted(quality["scenes"], key=lambda r: (-r["global_top1_ratio"], r["scene_id"]))[:15]
rows = []
for row in best:
    rows.append(
        f"<tr><td><a href=\"{rel_href(selfcheckdir / row['review_html'])}\">{row['scene_id']}</a></td>"
        f"<td>{row['expected_top1_ratio']:.3f}</td>"
        f"<td>{row['global_top1_ratio']:.3f}</td>"
        f"<td>{row['sprite_visible_ratio']:.3f}</td>"
        f"<td>{row['ocean_ratio']:.3f}</td>"
        f"<td>{row['dominant_global_match_scene']}</td>"
        f"<td>{row['dominant_failure_mode']}</td></tr>"
    )

html = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Vision Reference Pipeline</title>
  <style>
    body {{ font-family: sans-serif; margin: 24px; }}
    table {{ border-collapse: collapse; width: 100%; }}
    td, th {{ border: 1px solid #ccc; padding: 8px; text-align: left; }}
    .links a {{ display: block; margin: 6px 0; }}
  </style>
</head>
<body>
  <h1>Vision Reference Pipeline</h1>
  <div class="links">
    <a href="{rel_href(bankdir / 'index.html')}">Reference bank index</a>
    <a href="{rel_href(selfcheckdir / 'index.html')}">Reference self-check index</a>
    <a href="{rel_href(selfcheckdir / 'quality-report.html')}">Quality report</a>
    <a href="{rel_href(manifest_path)}">Manifest JSON</a>
  </div>
  <p>Reference scenes: {len(bank['scenes'])}. Reference frames: {bank['frame_count']}.</p>
  <h2>Top Global Self-Matches</h2>
  <table>
    <thead>
      <tr><th>Scene</th><th>Expected Top1</th><th>Global Top1</th><th>Sprites</th><th>Ocean</th><th>Dominant Global</th><th>Failure Mode</th></tr>
    </thead>
    <tbody>
      {''.join(rows)}
    </tbody>
  </table>
</body>
</html>
"""
html_path.write_text(html, encoding="utf-8")
print(html_path)
PY

echo "Pipeline complete:"
echo "  $MANIFEST_HTML"
