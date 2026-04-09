#!/bin/bash
# capture-host-reference.sh — Capture a deterministic desktop/host scene frame
# and emit reference metadata compatible with compare-scene-reference.py.

set -euo pipefail

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
HOST_BIN="$PROJECT_ROOT/build-host/jc_reborn"
RES_DIR="$PROJECT_ROOT/jc_resources"
SCENE_LIST_FILE="$PROJECT_ROOT/config/ps1/regtest-scenes.txt"

SCENE=""
BOOT=""
OUTPUT_ROOT="$PROJECT_ROOT/host-references"
FRAME_NO=60
SEED="1"
MODE="story-direct"
ISLAND_X=""
ISLAND_Y=""
CAPTURE_OVERLAY=0

usage() {
    cat <<'USAGE'
Usage: capture-host-reference.sh --scene "ADS TAG" [OPTIONS]

Options:
  --scene "ADS TAG"    Scene label, e.g. "BUILDING 1"
  --boot STRING        Explicit host boot string
  --frame N            Capture frame number N (default: 60)
  --seed N             Force deterministic RNG seed (default: 1)
  --island-x N         Force island X position
  --island-y N         Force island Y position
  --mode NAME          Boot mode: story-direct, story-single, ads (default: story-direct)
  --output DIR         Output root (default: host-references/)
  --overlay            Embed debug overlay blocks in captured screenshot
  -h, --help           Show this help
USAGE
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --scene)  SCENE="$2"; shift 2 ;;
        --boot)   BOOT="$2"; shift 2 ;;
        --frame)  FRAME_NO="$2"; shift 2 ;;
        --seed)   SEED="$2"; shift 2 ;;
        --island-x) ISLAND_X="$2"; shift 2 ;;
        --island-y) ISLAND_Y="$2"; shift 2 ;;
        --mode)   MODE="$2"; shift 2 ;;
        --output) OUTPUT_ROOT="$2"; shift 2 ;;
        --overlay) CAPTURE_OVERLAY=1; shift ;;
        -h|--help) usage ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if [ -z "$SCENE" ]; then
    echo "ERROR: --scene is required." >&2
    usage
fi

if [ ! -x "$HOST_BIN" ]; then
    echo "ERROR: Host binary not found: $HOST_BIN" >&2
    echo "Run ./scripts/build-host.sh first." >&2
    exit 1
fi

if [ ! -f "$RES_DIR/RESOURCE.MAP" ] || [ ! -f "$RES_DIR/RESOURCE.001" ]; then
    echo "ERROR: Host resources missing in $RES_DIR" >&2
    exit 1
fi

if ! command -v xvfb-run >/dev/null 2>&1; then
    echo "ERROR: xvfb-run not found. Install xvfb first." >&2
    exit 2
fi

read -r ADS_NAME TAG <<< "$SCENE"
SCENE_LABEL="${ADS_NAME}-${TAG}"
OUT_DIR="$OUTPUT_ROOT/$SCENE_LABEL"
mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

SCENE_INDEX=""
STATUS=""
if [ -f "$SCENE_LIST_FILE" ]; then
    while read -r scene_ads scene_tag scene_index scene_status _rest; do
        [ -n "${scene_ads:-}" ] || continue
        if [ "$scene_ads" = "$ADS_NAME" ] && [ "$scene_tag" = "$TAG" ]; then
            SCENE_INDEX="$scene_index"
            STATUS="$scene_status"
            break
        fi
    done < <(grep -v '^[[:space:]]*#' "$SCENE_LIST_FILE" | sed '/^[[:space:]]*$/d')
fi

FRAME_BMP="$OUT_DIR/frame_$(printf '%05d' "$FRAME_NO").bmp"
FRAME_SCENE_PNG="$OUT_DIR/frame_$(printf '%05d' "$FRAME_NO").scene.png"
VISUAL_JSON="$OUT_DIR/visual.json"
META_JSON="$OUT_DIR/metadata.json"
FRAME_META_JSON="$OUT_DIR/frame_$(printf '%05d' "$FRAME_NO").json"

if [ -z "$BOOT" ]; then
    case "$MODE" in
        story-direct)
            if [ -z "$SCENE_INDEX" ]; then
                echo "ERROR: scene index required for --mode story-direct" >&2
                exit 1
            fi
            BOOT="window nosound story direct ${SCENE_INDEX}"
            ;;
        story-single)
            if [ -z "$SCENE_INDEX" ]; then
                echo "ERROR: scene index required for --mode story-single" >&2
                exit 1
            fi
            BOOT="window nosound story single ${SCENE_INDEX}"
            ;;
        ads)
            BOOT="window nosound island ads ${ADS_NAME}.ADS ${TAG}"
            ;;
        *)
            echo "ERROR: unknown --mode '$MODE'" >&2
            exit 1
            ;;
    esac
fi
if [ -n "$SEED" ]; then
    BOOT="$BOOT seed $SEED"
fi
if [ -n "$ISLAND_X" ] || [ -n "$ISLAND_Y" ]; then
    if [ -z "$ISLAND_X" ] || [ -z "$ISLAND_Y" ]; then
        echo "ERROR: --island-x and --island-y must be provided together" >&2
        exit 1
    fi
    BOOT="$BOOT island-pos $ISLAND_X $ISLAND_Y"
fi

CAPTURE_DATE="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

pushd "$RES_DIR" >/dev/null
capture_args=(
    capture-frame "$FRAME_NO"
    capture-output "$FRAME_BMP"
    capture-meta-dir "$OUT_DIR"
    capture-scene-label "$SCENE"
)
if [ "$CAPTURE_OVERLAY" -eq 1 ]; then
    capture_args+=(capture-overlay)
fi
xvfb-run -a env SDL_AUDIODRIVER=dummy "$HOST_BIN" \
    $BOOT \
    "${capture_args[@]}"
popd >/dev/null

python3 "$PROJECT_ROOT/scripts/visual_detect.py" --json "$FRAME_BMP" > "$VISUAL_JSON"
python3 "$PROJECT_ROOT/scripts/normalize-scene-frame.py" \
    "$FRAME_BMP" \
    --output "$FRAME_SCENE_PNG" >/dev/null

python3 - "$FRAME_BMP" "$META_JSON" "$VISUAL_JSON" "$SCENE_LIST_FILE" \
           "$ADS_NAME" "$TAG" "$FRAME_NO" "$CAPTURE_DATE" "$BOOT" "$SEED" "$MODE" "$ISLAND_X" "$ISLAND_Y" \
           "$FRAME_SCENE_PNG" "$FRAME_META_JSON" "$CAPTURE_OVERLAY" <<'PY'
import hashlib
import json
import sys
from pathlib import Path

from PIL import Image

frame_path = Path(sys.argv[1])
meta_path = Path(sys.argv[2])
visual_path = Path(sys.argv[3])
scene_list_path = Path(sys.argv[4])
ads_name = sys.argv[5]
tag = int(sys.argv[6])
frame_no = int(sys.argv[7])
capture_date = sys.argv[8]
boot_string = sys.argv[9]
forced_seed = int(sys.argv[10]) if sys.argv[10] else None
mode = sys.argv[11]
forced_island_x = int(sys.argv[12]) if sys.argv[12] else None
forced_island_y = int(sys.argv[13]) if sys.argv[13] else None
scene_png_path = Path(sys.argv[14])
frame_meta_path = Path(sys.argv[15])
capture_overlay = int(sys.argv[16]) != 0

output_dir = meta_path.parent

def rel_to_output(path: Path) -> str:
    return path.resolve().relative_to(output_dir.resolve()).as_posix()

visual = json.loads(visual_path.read_text(encoding="utf-8"))
raw_sha = hashlib.sha256(frame_path.read_bytes()).hexdigest()
scene_png_sha = hashlib.sha256(scene_png_path.read_bytes()).hexdigest()

with Image.open(frame_path) as img:
    rgb = img.convert("RGB")
    pixel_sha = hashlib.sha256(rgb.tobytes()).hexdigest()
    if rgb.size == (640, 480):
        scene_rgb = rgb.crop((0, 16, 640, 464))
    else:
        scene_rgb = rgb
    scene_rgba = scene_rgb.convert("RGBA")
    for x0, y0, x1, y1 in (
        (0, 0, 110, 260),
        (500, 124, 618, 344),
        (548, 16, 616, 84),
    ):
        for y in range(y0, min(y1, scene_rgba.height)):
            for x in range(x0, min(x1, scene_rgba.width)):
                scene_rgba.putpixel((x, y), (0, 0, 0, 255))
    scene_pixel_sha = hashlib.sha256(scene_rgba.tobytes()).hexdigest()

scene_index = None
status = None
if scene_list_path.is_file():
    for raw in scene_list_path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) < 4:
            continue
        if parts[0] == ads_name and parts[1] == str(tag):
            scene_index = int(parts[2])
            status = parts[3]
            break

frame_entry = {
    "frame": frame_path.name,
    "frame_path": rel_to_output(frame_path),
    "frame_scene_path": rel_to_output(scene_png_path),
    "frame_sha256": raw_sha,
    "frame_pixel_sha256": pixel_sha,
    "frame_scene_pixel_sha256": scene_pixel_sha,
    "frame_scene_png_sha256": scene_png_sha,
    "frame_number": frame_no,
}

metadata = {
    "ads_name": ads_name,
    "ads_file": f"{ads_name}.ADS",
    "tag": tag,
    "scene_index": scene_index,
    "status": status,
    "boot_string": boot_string,
    "mode": mode,
    "forced_seed": forced_seed,
    "forced_island_position": (
        {"x": forced_island_x, "y": forced_island_y}
        if forced_island_x is not None and forced_island_y is not None else None
    ),
    "capture_date": capture_date,
    "capture_overlay": capture_overlay,
    "frame_count": 1,
    "frames": [rel_to_output(frame_path)],
    "frame_meta_files": [rel_to_output(frame_meta_path)] if frame_meta_path.exists() else [],
    "image_size": visual.get("image_size"),
    "visual_last": visual,
    "visual_best": frame_entry,
    "visual_entry_scene": frame_entry,
    "visual_last_scene": frame_entry,
}

meta_path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
PY

echo "Captured host reference: $META_JSON"
