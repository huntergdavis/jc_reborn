#!/bin/bash
# capture-host-baseline.sh — Capture dense host baselines for one or more scenes
# into a timestamped root with per-scene review pages and a master index.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SCENE_SCRIPT="$PROJECT_ROOT/scripts/capture-host-scene.sh"
DEFAULT_SCENE_LIST="$PROJECT_ROOT/config/ps1/regtest-scenes.txt"
DEFAULT_OUTPUT_BASE="$PROJECT_ROOT/host-baselines"

SCENE_LIST="$DEFAULT_SCENE_LIST"
OUTPUT_BASE="$DEFAULT_OUTPUT_BASE"
OUTPUT_ROOT=""
SEED=1
INTERVAL=1
ONLY_SCENE=""
LIMIT=""

usage() {
    cat <<'USAGE'
Usage: capture-host-baseline.sh [OPTIONS]

Options:
  --scene-list FILE       Scene list file (default: config/ps1/regtest-scenes.txt)
  --output-base DIR       Parent directory for timestamped baseline roots
  --output-root DIR       Exact output root to use instead of auto timestamping
  --seed N                Deterministic RNG seed (default: 1)
  --interval N            Capture every Nth frame (default: 1)
  --scene "ADS TAG"       Capture only one scene, e.g. "BUILDING 1"
  --limit N               Capture only the first N matching scenes
  -h, --help              Show this help
USAGE
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --scene-list) SCENE_LIST="$2"; shift 2 ;;
        --output-base) OUTPUT_BASE="$2"; shift 2 ;;
        --output-root) OUTPUT_ROOT="$2"; shift 2 ;;
        --seed) SEED="$2"; shift 2 ;;
        --interval) INTERVAL="$2"; shift 2 ;;
        --scene) ONLY_SCENE="$2"; shift 2 ;;
        --limit) LIMIT="$2"; shift 2 ;;
        -h|--help) usage ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if [ ! -x "$SCENE_SCRIPT" ]; then
    echo "ERROR: capture script not found: $SCENE_SCRIPT" >&2
    exit 1
fi

if [ ! -f "$SCENE_LIST" ]; then
    echo "ERROR: scene list not found: $SCENE_LIST" >&2
    exit 1
fi

if [ -z "$OUTPUT_ROOT" ]; then
    stamp="$(date -u +%Y%m%d-%H%M%S)"
    OUTPUT_ROOT="${OUTPUT_BASE%/}/${stamp}-host-baseline-seed${SEED}"
fi

mkdir -p "$OUTPUT_ROOT"
OUTPUT_ROOT="$(cd "$OUTPUT_ROOT" && pwd)"

MANIFEST_JSON="$OUTPUT_ROOT/manifest.json"
MANIFEST_CSV="$OUTPUT_ROOT/manifest.csv"
INDEX_HTML="$OUTPUT_ROOT/index.html"

python3 - "$SCENE_LIST" "$OUTPUT_ROOT" "$MANIFEST_JSON" "$MANIFEST_CSV" "$INDEX_HTML" "$ONLY_SCENE" "$LIMIT" <<'PY'
import csv
import html
import json
import sys
from pathlib import Path

scene_list = Path(sys.argv[1])
output_root = Path(sys.argv[2])
manifest_json = Path(sys.argv[3])
manifest_csv = Path(sys.argv[4])
index_html = Path(sys.argv[5])
only_scene = sys.argv[6].strip()
limit_raw = sys.argv[7].strip()
limit = int(limit_raw) if limit_raw else None

rows = []
for raw in scene_list.read_text(encoding="utf-8").splitlines():
    line = raw.split("#", 1)[0].strip()
    if not line:
        continue
    parts = line.split()
    if len(parts) < 4:
        continue
    ads_name = parts[0]
    tag = parts[1]
    scene_index = int(parts[2])
    status = parts[3]
    boot_tokens = parts[4:]
    scene_label = f"{ads_name} {tag}"
    if only_scene and scene_label != only_scene:
        continue
    slug = f"{ads_name}-{tag}"
    rows.append({
        "scene_label": scene_label,
        "ads_name": ads_name,
        "ads_file": f"{ads_name}.ADS",
        "tag": int(tag),
        "scene_index": scene_index,
        "status": status,
        "boot_tokens": boot_tokens,
        "boot_mode": " ".join(boot_tokens),
        "capture_mode": "engine-truth scene-end",
        "slug": slug,
        "output_dir": str((output_root / slug).resolve()),
        "review_html": str((output_root / slug / "review.html").resolve()),
        "result_json": str((output_root / slug / "result.json").resolve()),
    })
    if limit is not None and len(rows) >= limit:
        break

manifest_json.write_text(json.dumps({"rows": rows}, indent=2) + "\n", encoding="utf-8")

with manifest_csv.open("w", encoding="utf-8", newline="") as f:
    writer = csv.DictWriter(
        f,
        fieldnames=[
            "scene_label",
            "ads_name",
            "tag",
            "scene_index",
            "status",
            "boot_mode",
            "capture_mode",
            "slug",
            "output_dir",
            "review_html",
            "result_json",
        ],
    )
    writer.writeheader()
    for row in rows:
        writer.writerow({k: row[k] for k in writer.fieldnames})

cards = []
for row in rows:
    slug = html.escape(row["slug"])
    scene_label = html.escape(row["scene_label"])
    capture_mode = html.escape(row["capture_mode"])
    cards.append(
        f'<li><a href="{slug}/review.html">{scene_label}</a> '
        f'(<code>{capture_mode}</code>) '
        f'boot=<code>{html.escape(row["boot_mode"])}</code> '
        f'<span>{html.escape(row["status"])}</span></li>'
    )

index_html.write_text(
    "<!doctype html>\n"
    "<html><head><meta charset=\"utf-8\">"
    "<title>Host Baseline Review</title>"
    "<style>"
    "body{margin:0;font:14px/1.4 monospace;background:#0b0f14;color:#e6edf3;}"
    "main{max-width:1100px;margin:0 auto;padding:24px;}"
    "a{color:#8bd5ff;text-decoration:none;}"
    "code{color:#ffd580;}"
    "li{margin:0 0 10px 0;}"
    ".meta{color:#9fb0c0;margin-bottom:16px;}"
    "</style></head><body><main>"
    "<h1>Host Baseline Review</h1>"
    f"<div class=\"meta\">root: {html.escape(str(output_root))}</div>"
    f"<div class=\"meta\">scene_count: {len(rows)}</div>"
    "<ul>"
    + "".join(cards)
    + "</ul></main></body></html>\n",
    encoding="utf-8",
)
PY

mapfile -t manifest_rows < <(python3 - "$MANIFEST_JSON" <<'PY'
import json
import sys
from pathlib import Path

manifest = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
for row in manifest["rows"]:
    print("\t".join([
        row["scene_label"],
        row["output_dir"],
        row["boot_mode"],
    ]))
PY
)

total_scenes="${#manifest_rows[@]}"
scene_index=0

for manifest_row in "${manifest_rows[@]}"; do
    scene_index=$((scene_index + 1))
    IFS=$'\t' read -r scene_label scene_output_dir boot_mode <<< "$manifest_row"
    echo "[$scene_index/$total_scenes] BEGIN $scene_label boot=\"$boot_mode\""
    "$SCENE_SCRIPT" \
        --scene "$scene_label" \
        --mode scene-default \
        --seed "$SEED" \
        --interval "$INTERVAL" \
        --until-exit \
        --output "$scene_output_dir" \
        --skip-visual-detect \
        --no-stamp
    frames_captured="$(python3 - "$scene_output_dir/result.json" <<'PY'
import json
import sys
from pathlib import Path
result = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
print(result.get("outcome", {}).get("frames_captured", "unknown"))
PY
)"
    echo "[$scene_index/$total_scenes] DONE $scene_label frames_captured=$frames_captured"
done

echo "Wrote baseline root:"
echo "  $OUTPUT_ROOT"
echo "Index:"
echo "  $INDEX_HTML"
