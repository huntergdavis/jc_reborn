#!/bin/bash
# capture-host-scene.sh — Run a desktop scene under xvfb, capture sampled
# frames, and emit a result.json compatible with compare-scene-reference.py.

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
OUTPUT_DIR="$PROJECT_ROOT/host-results/scene"
FRAMES=600
START_FRAME=0
START_FRAME_EXPLICIT=0
MIN_TAIL_FRAMES="${HOST_SCENE_CAPTURE_MIN_TAIL_FRAMES:-300}"
INTERVAL=60
FRAME_LIST=""
DURATION_TABLE=""
SEED="1"
MODE="scene-default"
STORY_DAY=""
ISLAND_X=""
ISLAND_Y=""
LOWTIDE=""
RAFT_STAGE=""
SCENE_OFFSET_X=""
SCENE_OFFSET_Y=""
SKIP_VISUAL_DETECT=1
STAMP_PREFIX=1
UNTIL_EXIT=0
CAPTURE_OVERLAY=0
TIMEOUT_SECONDS=""

usage() {
    cat <<'USAGE'
Usage: capture-host-scene.sh --scene "ADS TAG" [OPTIONS]

Options:
  --scene "ADS TAG"    Scene label, e.g. "BUILDING 1"
  --boot STRING        Explicit host boot string
  --frames N           Capture through frame N (default: 600)
  --start-frame N     Start sampling at frame N instead of the reviewed scene start
  --frame-list LIST    Capture specific frame numbers, e.g. 0,20,40,80
  --until-exit         Capture every requested frame until the executable returns
  --duration-table P   JSON from estimate-scene-durations.py; used when --frames auto
  --interval N         Capture every Nth frame (default: 60)
  --seed N             Force deterministic RNG seed (default: 1)
  --story-day N        Force story day 1..11 for host story runs
  --island-x N         Force island X position
  --island-y N         Force island Y position
  --lowtide 0|1        Force low tide state
  --raft-stage N       Force raft stage 0..5
  --scene-offset-x N   Force thread-layer scene X offset
  --scene-offset-y N   Force thread-layer scene Y offset
  --mode NAME          Boot mode: scene-default, scene-exact, story-direct, story-hold, story-single, ads (default: scene-default)
  --output DIR         Output directory (default: host-results/scene)
  --timeout N          Kill host run after N seconds; 0 disables timeout (default: auto)
  --visual-detect      Run expensive visual_detect.py postprocess
  --skip-visual-detect Skip expensive visual_detect.py postprocess (default)
  --no-stamp           Do not prefix the output leaf with UTC timestamp
  --overlay            Embed debug overlay blocks in captured screenshots
  -h, --help           Show this help
USAGE
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --scene) SCENE="$2"; shift 2 ;;
        --boot) BOOT="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --start-frame) START_FRAME="$2"; START_FRAME_EXPLICIT=1; shift 2 ;;
        --frame-list) FRAME_LIST="$2"; shift 2 ;;
        --until-exit) UNTIL_EXIT=1; shift ;;
        --duration-table) DURATION_TABLE="$2"; shift 2 ;;
        --interval) INTERVAL="$2"; shift 2 ;;
        --seed) SEED="$2"; shift 2 ;;
        --story-day) STORY_DAY="$2"; shift 2 ;;
        --island-x) ISLAND_X="$2"; shift 2 ;;
        --island-y) ISLAND_Y="$2"; shift 2 ;;
        --lowtide) LOWTIDE="$2"; shift 2 ;;
        --raft-stage) RAFT_STAGE="$2"; shift 2 ;;
        --scene-offset-x) SCENE_OFFSET_X="$2"; shift 2 ;;
        --scene-offset-y) SCENE_OFFSET_Y="$2"; shift 2 ;;
        --mode) MODE="$2"; shift 2 ;;
        --output) OUTPUT_DIR="$2"; shift 2 ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        --visual-detect) SKIP_VISUAL_DETECT=0; shift ;;
        --skip-visual-detect) SKIP_VISUAL_DETECT=1; shift ;;
        --no-stamp) STAMP_PREFIX=0; shift ;;
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
ADS_FILE="${ADS_NAME}.ADS"
if [ "$STAMP_PREFIX" -eq 1 ]; then
    stamp="$(date -u +%Y%m%d-%H%M%S)"
    OUTPUT_DIR="${OUTPUT_DIR%/}/${stamp}-$(basename "$OUTPUT_DIR")"
fi
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"
FRAMES_DIR="$OUTPUT_DIR/frames"
FRAME_META_DIR="$OUTPUT_DIR/frame-meta"
mkdir -p "$FRAMES_DIR"
mkdir -p "$FRAME_META_DIR"

SCENE_INDEX=""
STATUS=""
SCENE_BOOT_TOKENS=""
if [ -f "$SCENE_LIST_FILE" ]; then
    while read -r scene_ads scene_tag scene_index scene_status boot1 boot2 boot3 boot4 boot5 boot6; do
        [ -n "${scene_ads:-}" ] || continue
        if [ "$scene_ads" = "$ADS_NAME" ] && [ "$scene_tag" = "$TAG" ]; then
            SCENE_INDEX="$scene_index"
            STATUS="$scene_status"
            SCENE_BOOT_TOKENS="${boot1:-} ${boot2:-} ${boot3:-} ${boot4:-} ${boot5:-} ${boot6:-}"
            break
        elif [ "$scene_ads" = "$ADS_FILE" ] && [ "$scene_tag" = "$TAG" ]; then
            SCENE_INDEX="$scene_index"
            STATUS="$scene_status"
            SCENE_BOOT_TOKENS="${boot1:-} ${boot2:-} ${boot3:-} ${boot4:-} ${boot5:-} ${boot6:-}"
            break
        fi
    done < <(grep -v '^[[:space:]]*#' "$SCENE_LIST_FILE" | sed '/^[[:space:]]*$/d')
fi

SCENE_BOOT_TOKENS="$(echo "$SCENE_BOOT_TOKENS" | xargs)"

if [ -z "$BOOT" ]; then
    case "$MODE" in
        scene-default)
            if [ -z "$SCENE_BOOT_TOKENS" ]; then
                echo "ERROR: scene-default requires boot tokens in $SCENE_LIST_FILE" >&2
                exit 1
            fi
            BOOT="window nosound ${SCENE_BOOT_TOKENS}"
            ;;
        scene-exact)
            if [ -z "$SCENE_BOOT_TOKENS" ]; then
                echo "ERROR: scene-exact requires boot tokens in $SCENE_LIST_FILE" >&2
                exit 1
            fi
            if [[ "$SCENE_BOOT_TOKENS" =~ ^story[[:space:]]+(scene|index)[[:space:]]+[0-9]+$ ]]; then
                if [ -z "$SCENE_INDEX" ]; then
                    echo "ERROR: scene-exact requires scene index for story scene entries" >&2
                    exit 1
                fi
                BOOT="window nosound story direct ${SCENE_INDEX}"
            else
                BOOT="window nosound ${SCENE_BOOT_TOKENS}"
            fi
            ;;
        story-direct)
            if [ -z "$SCENE_INDEX" ]; then
                echo "ERROR: scene index required for --mode story-direct" >&2
                exit 1
            fi
            BOOT="window nosound story direct ${SCENE_INDEX}"
            ;;
        story-hold)
            if [ -z "$SCENE_INDEX" ]; then
                echo "ERROR: scene index required for --mode story-hold" >&2
                exit 1
            fi
            BOOT="window nosound story hold ${SCENE_INDEX}"
            ;;
        story-single)
            if [ -z "$SCENE_INDEX" ]; then
                echo "ERROR: scene index required for --mode story-single" >&2
                exit 1
            fi
            BOOT="window nosound story single ${SCENE_INDEX}"
            ;;
        ads)
            BOOT="window nosound island ads ${ADS_FILE} ${TAG}"
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
if [ -n "$STORY_DAY" ]; then
    BOOT="$BOOT story-day $STORY_DAY"
fi
if [ -n "$ISLAND_X" ] || [ -n "$ISLAND_Y" ]; then
    if [ -z "$ISLAND_X" ] || [ -z "$ISLAND_Y" ]; then
        echo "ERROR: --island-x and --island-y must be provided together" >&2
        exit 1
    fi
    BOOT="$BOOT island-pos $ISLAND_X $ISLAND_Y"
fi
if [ -n "$LOWTIDE" ]; then
    BOOT="$BOOT lowtide $LOWTIDE"
fi
if [ -n "$RAFT_STAGE" ]; then
    BOOT="$BOOT raft-stage $RAFT_STAGE"
fi
if [ -n "$SCENE_OFFSET_X" ] || [ -n "$SCENE_OFFSET_Y" ]; then
    if [ -z "$SCENE_OFFSET_X" ] || [ -z "$SCENE_OFFSET_Y" ]; then
        echo "ERROR: --scene-offset-x and --scene-offset-y must be provided together" >&2
        exit 1
    fi
    BOOT="$BOOT scene-offset $SCENE_OFFSET_X $SCENE_OFFSET_Y"
fi

if [ "$FRAMES" = "auto" ]; then
    if [ -z "$DURATION_TABLE" ]; then
        echo "ERROR: --frames auto requires --duration-table" >&2
        exit 1
    fi
    FRAMES="$(python3 - "$DURATION_TABLE" "$ADS_FILE" "$TAG" <<'PY'
import json
import sys
from pathlib import Path

table = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
ads_name = sys.argv[2]
ads_tag = int(sys.argv[3])

for row in table["rows"]:
    if row["ads_name"] == ads_name and int(row["ads_tag"]) == ads_tag:
        value = row.get("estimated_frames")
        if value is None:
            raise SystemExit("duration table row has no estimated_frames")
        print(int(value))
        raise SystemExit(0)

raise SystemExit("scene not found in duration table")
PY
)"
fi

if [ "$START_FRAME_EXPLICIT" -eq 0 ] && [ -z "$FRAME_LIST" ]; then
    START_FRAME="$(python3 "$SCRIPT_DIR/get-scene-capture-start.py" --scene "$SCENE")"
fi

if [ "$START_FRAME_EXPLICIT" -eq 0 ] && [ "$UNTIL_EXIT" -eq 0 ] && [ -z "$FRAME_LIST" ]; then
    min_frames=$((START_FRAME + MIN_TAIL_FRAMES))
    if [ "$FRAMES" -lt "$min_frames" ]; then
        FRAMES="$min_frames"
    fi
fi

if [ -n "$FRAME_LIST" ]; then
    if [ "$UNTIL_EXIT" -eq 1 ]; then
        echo "ERROR: --frame-list cannot be combined with --until-exit" >&2
        exit 1
    fi
    mapfile -t frame_list_numbers < <(python3 - "$FRAME_LIST" <<'PY'
import math
import sys

raw = sys.argv[1]
parts = [p.strip() for p in raw.split(",") if p.strip()]
if not parts:
    raise SystemExit("empty frame list")
frames = []
for part in parts:
    value = int(part)
    if value < 0:
        raise SystemExit("frame list values must be >= 0")
    frames.append(value)
frames = sorted(set(frames))
interval = 1
if len(frames) > 1:
    interval = frames[1] - frames[0]
    for a, b in zip(frames, frames[1:]):
        interval = math.gcd(interval, b - a)
print(frames[0])
print(frames[-1])
print(interval)
print(",".join(str(v) for v in frames))
PY
    )
    START_FRAME="${frame_list_numbers[0]}"
    FRAMES="${frame_list_numbers[1]}"
    INTERVAL="${frame_list_numbers[2]}"
    FRAME_LIST="${frame_list_numbers[3]}"
fi

if [ "$START_FRAME" -lt 0 ]; then
    echo "ERROR: --start-frame must be >= 0" >&2
    exit 1
fi
if [ "$UNTIL_EXIT" -eq 0 ] && [ "$FRAMES" -lt "$START_FRAME" ]; then
    echo "ERROR: --frames must be >= --start-frame" >&2
    exit 1
fi

if [ -z "$TIMEOUT_SECONDS" ]; then
    if [ "$UNTIL_EXIT" -eq 1 ]; then
        TIMEOUT_SECONDS=0
    elif [ "$FRAMES" -le 120 ]; then
        TIMEOUT_SECONDS=60
    elif [ "$FRAMES" -le 300 ]; then
        TIMEOUT_SECONDS=90
    else
        TIMEOUT_SECONDS=180
    fi
fi

capture_ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

pushd "$RES_DIR" >/dev/null
capture_args=(
    capture-dir "$FRAMES_DIR"
    capture-meta-dir "$FRAME_META_DIR"
    capture-scene-label "$SCENE"
    capture-interval "$INTERVAL"
)
if [ "$CAPTURE_OVERLAY" -eq 1 ]; then
    capture_args+=(capture-overlay)
fi
if [ "$UNTIL_EXIT" -eq 1 ]; then
    capture_args+=(capture-range "$START_FRAME" -1)
else
    capture_args+=(capture-range "$START_FRAME" "$FRAMES")
fi

set +e
if [ "$TIMEOUT_SECONDS" -gt 0 ]; then
    timeout "$TIMEOUT_SECONDS" xvfb-run -a env SDL_AUDIODRIVER=dummy "$HOST_BIN" \
        $BOOT \
        "${capture_args[@]}"
    host_exit_code=$?
else
    xvfb-run -a env SDL_AUDIODRIVER=dummy "$HOST_BIN" \
        $BOOT \
        "${capture_args[@]}"
    host_exit_code=$?
fi
set -e
popd >/dev/null

python3 - "$PROJECT_ROOT" "$OUTPUT_DIR" "$SCENE_LIST_FILE" "$ADS_NAME" "$TAG" \
           "$BOOT" "$SEED" "$capture_ts" "$FRAMES" "$START_FRAME" "$INTERVAL" "$MODE" "$ISLAND_X" "$ISLAND_Y" "$LOWTIDE" "$SKIP_VISUAL_DETECT" "$UNTIL_EXIT" "$host_exit_code" "$CAPTURE_OVERLAY" "$TIMEOUT_SECONDS" "$FRAME_LIST" <<'PY'
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

from PIL import Image

project_root = Path(sys.argv[1])
output_dir = Path(sys.argv[2])
scene_list_path = Path(sys.argv[3])
ads_name = sys.argv[4]
tag = int(sys.argv[5])
boot_string = sys.argv[6]
forced_seed = int(sys.argv[7]) if sys.argv[7] else None
capture_ts = sys.argv[8]
requested_frames = int(sys.argv[9])
requested_start_frame = int(sys.argv[10])
requested_interval = int(sys.argv[11])
mode = sys.argv[12]
forced_island_x = int(sys.argv[13]) if sys.argv[13] else None
forced_island_y = int(sys.argv[14]) if sys.argv[14] else None
forced_low_tide = int(sys.argv[15]) if sys.argv[15] else None
skip_visual_detect = int(sys.argv[16]) != 0
until_exit = int(sys.argv[17]) != 0
host_exit_code = int(sys.argv[18])
capture_overlay = int(sys.argv[19]) != 0
timeout_seconds = int(sys.argv[20]) if sys.argv[20] else 0
frame_list_raw = sys.argv[21].strip()
frames_dir = output_dir / "frames"
frame_meta_dir = output_dir / "frame-meta"

def list_frame_files() -> list[Path]:
    return sorted(frames_dir.glob("**/frame_*.bmp"))

def list_meta_files() -> list[Path]:
    return sorted(frame_meta_dir.glob("**/frame_*.json"))

frame_list = []
if frame_list_raw:
    frame_list = [int(part) for part in frame_list_raw.split(",") if part]
    keep_frame_names = {f"frame_{frame_no:05d}.bmp" for frame_no in frame_list}
    keep_meta_names = {f"frame_{frame_no:05d}.json" for frame_no in frame_list}
    for frame_path in list_frame_files():
        if frame_path.name not in keep_frame_names:
            frame_path.unlink()
    for meta_path in list_meta_files():
        if meta_path.name not in keep_meta_names:
            meta_path.unlink()


def rel_to_output(path: Path) -> str:
    return path.resolve().relative_to(output_dir.resolve()).as_posix()

frame_files = list_frame_files()
png_dir = output_dir / "frames-png"
png_dir.mkdir(exist_ok=True)
visual_entries = []
if not skip_visual_detect:
    for frame_path in frame_files:
        proc = subprocess.run(
            [sys.executable, str(project_root / "scripts" / "visual_detect.py"), "--json", str(frame_path)],
            check=True,
            capture_output=True,
            text=True,
        )
        data = json.loads(proc.stdout)
        visual_entries.append(data)

(output_dir / "visual-batch.json").write_text(json.dumps(visual_entries, indent=2) + "\n", encoding="utf-8")
if visual_entries:
    (output_dir / "visual.json").write_text(json.dumps(visual_entries[-1], indent=2) + "\n", encoding="utf-8")
else:
    (output_dir / "visual.json").write_text("{}\n", encoding="utf-8")

png_files = []
for frame_path in frame_files:
    png_path = png_dir / f"{frame_path.stem}.png"
    if not png_path.exists():
        with Image.open(frame_path) as img:
            img.save(png_path)
    png_files.append(png_path)

for meta_path in list_meta_files():
    meta = json.loads(meta_path.read_text(encoding="utf-8"))
    image_path = meta.get("image_path")
    if image_path:
        resolved = Path(image_path)
        if not resolved.is_absolute():
            resolved = (meta_path.parent / image_path).resolve()
        meta["image_path"] = rel_to_output(resolved)
        meta_path.write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")

def file_hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def pixel_hash(path: Path) -> str:
    with Image.open(path) as img:
        return hashlib.sha256(img.convert("RGBA").tobytes()).hexdigest()

def scene_pixel_hash(path: Path) -> str:
    with Image.open(path) as img:
        rgba = img.convert("RGBA")
        if rgba.size == (640, 480):
            rgba = rgba.crop((0, 16, 640, 464))
        for x0, y0, x1, y1 in (
            (0, 0, 110, 260),
            (500, 124, 618, 344),
            (548, 16, 616, 84),
        ):
            for y in range(y0, min(y1, rgba.height)):
                for x in range(x0, min(x1, rgba.width)):
                    rgba.putpixel((x, y), (0, 0, 0, 255))
        return hashlib.sha256(rgba.tobytes()).hexdigest()

def annotate_frame(entry: dict, file_path: Path) -> dict:
    out = dict(entry)
    out["frame"] = file_path.name
    out["frame_path"] = rel_to_output(file_path)
    out["frame_sha256"] = file_hash(file_path)
    px = pixel_hash(file_path)
    out["frame_pixel_sha256"] = px
    out["frame_scene_pixel_sha256"] = scene_pixel_hash(file_path)
    return out

timeline = []
best_entry = None
best_path = None
best_score = -1
last_scene_entry = None
last_scene_path = None
entry_scene_entry = None
entry_scene_path = None
late_scene_started = False
seen_bootstrap_scene = False

for entry in visual_entries:
    file_path = Path(entry.get("file", ""))
    if not file_path.is_file():
        continue
    frame_name = file_path.name
    screen_type = entry.get("screen_type", {}).get("type")
    scene_family = entry.get("scene_family", {}).get("family")
    island_present = bool(entry.get("island", {}).get("present"))
    johnny_present = any(
        sp.get("name") == "johnny" and sp.get("present")
        for sp in entry.get("sprites", [])
    )
    sand_present = any(
        sp.get("name") == "sand" and sp.get("present")
        for sp in entry.get("sprites", [])
    )
    score = 0
    if screen_type == "island":
        score += 8
    elif screen_type == "title":
        score += 2
    elif screen_type == "ocean":
        score += 1
    if island_present:
        score += 4
    if sand_present:
        score += 4
    if johnny_present:
        score += 8
    if score > best_score:
        best_score = score
        best_path = file_path
        best_entry = {
            "frame": frame_name,
            "screen_type": screen_type,
            "scene_family": scene_family,
            "island_present": island_present,
            "johnny_present": johnny_present,
            "sand_present": sand_present,
        }
    if screen_type == "island" or island_present or johnny_present or sand_present:
        if scene_family and scene_family != "unknown":
            seen_bootstrap_scene = True
    elif seen_bootstrap_scene:
        late_scene_started = True
    if late_scene_started and entry_scene_entry is None:
        if screen_type == "island" or island_present or johnny_present or sand_present:
            entry_scene_path = file_path
            entry_scene_entry = {
                "frame": frame_name,
                "screen_type": screen_type,
                "scene_family": scene_family,
                "island_present": island_present,
                "johnny_present": johnny_present,
                "sand_present": sand_present,
            }
    if screen_type == "island" or island_present or johnny_present or sand_present:
        last_scene_path = file_path
        last_scene_entry = {
            "frame": frame_name,
            "screen_type": screen_type,
            "scene_family": scene_family,
            "island_present": island_present,
            "johnny_present": johnny_present,
            "sand_present": sand_present,
        }
    timeline.append({
        "frame": frame_name,
        "screen_type": screen_type,
        "screen_confidence": entry.get("screen_type", {}).get("confidence"),
        "scene_family": scene_family,
        "island_present": island_present,
        "johnny_present": johnny_present,
        "sand_present": sand_present,
    })

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

state_hash = None
if frame_files:
    state_hash = scene_pixel_hash(frame_files[-1])

expected_frame_numbers = []
if frame_list:
    expected_frame_numbers = list(frame_list)
elif not until_exit and requested_interval > 0:
    expected_frame_numbers = list(range(requested_start_frame, requested_frames + 1, requested_interval))
expected_frame_names = {f"frame_{frame_no:05d}.bmp" for frame_no in expected_frame_numbers}
actual_frame_names = {path.name for path in frame_files}
missing_frames = sorted(expected_frame_names - actual_frame_names)
missing_meta = sorted(
    f"frame_{frame_no:05d}.json"
    for frame_no in expected_frame_numbers
    if not (frame_meta_dir / f"frame_{frame_no:05d}.json").is_file()
)
timed_out = host_exit_code == 124

outcome = {
    "exit_code": host_exit_code,
    "timed_out": timed_out,
    "frames_captured": len(frame_files),
    "expected_frames": len(expected_frame_numbers),
    "missing_frame_files": missing_frames,
    "missing_meta_files": missing_meta,
    "state_hash": state_hash,
    "has_fatal_error": bool(missing_frames or missing_meta or (timed_out and until_exit)),
    "likely_scene_not_started": False,
    "likely_scene_broken": False,
    "likely_visual_broken": False,
    "scene_markers_last": {
        "launched": False,
        "bmp_ok": False,
        "bmp_fail": False,
        "sprite_count_estimate": 0,
    },
    "visual_last": {},
    "visual_timeline": timeline,
}

if visual_entries:
    last = visual_entries[-1]
    outcome["visual_last"] = {
        "screen_type": last.get("screen_type", {}).get("type"),
        "island_present": bool(last.get("island", {}).get("present")),
        "johnny_present": any(sp.get("name") == "johnny" and sp.get("present") for sp in last.get("sprites", [])),
        "sand_present": any(sp.get("name") == "sand" and sp.get("present") for sp in last.get("sprites", [])),
    }

if best_entry and best_path:
    outcome["visual_best"] = annotate_frame(best_entry, best_path)
if entry_scene_entry is None and best_entry and best_path:
    entry_scene_entry = dict(best_entry)
    entry_scene_path = best_path
if entry_scene_entry and entry_scene_path:
    outcome["visual_entry_scene"] = annotate_frame(entry_scene_entry, entry_scene_path)
if last_scene_entry and last_scene_path:
    outcome["visual_last_scene"] = annotate_frame(last_scene_entry, last_scene_path)

result = {
    "scene": {
        "ads_name": ads_name,
        "tag": tag,
        "scene_index": scene_index,
        "status": status,
        "boot_string": boot_string,
        "mode": mode,
    },
    "config": {
        "start_frame": requested_start_frame,
        "frames": requested_frames,
        "interval": requested_interval,
        "until_exit": until_exit,
        "timeout": timeout_seconds,
        "cpu_mode": "host",
        "forced_seed": forced_seed,
        "forced_island_position": (
            {"x": forced_island_x, "y": forced_island_y}
            if forced_island_x is not None and forced_island_y is not None else None
        ),
        "forced_low_tide": forced_low_tide,
    },
    "outcome": outcome,
    "paths": {
        "output_dir": ".",
        "frames_dir": "frames",
        "frame_meta_dir": "frame-meta",
        "visual": "visual.json",
        "visual_batch": "visual-batch.json",
    },
}

(output_dir / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
(output_dir / "metadata.json").write_text(json.dumps({
    "ads_name": ads_name,
    "ads_file": f"{ads_name}.ADS",
    "tag": tag,
    "scene_index": scene_index,
    "status": status,
    "boot_string": boot_string,
    "mode": mode,
    "forced_seed": forced_seed,
    "start_frame": requested_start_frame,
    "forced_island_position": (
        {"x": forced_island_x, "y": forced_island_y}
        if forced_island_x is not None and forced_island_y is not None else None
    ),
    "forced_low_tide": forced_low_tide,
    "capture_overlay": capture_overlay,
    "frame_count": len(frame_files),
    "frames": [p.relative_to(output_dir).as_posix() for p in frame_files],
    "frame_meta_files": [p.relative_to(output_dir).as_posix() for p in list_meta_files()],
    "visual_last": outcome["visual_last"],
    "visual_best": outcome.get("visual_best"),
    "visual_entry_scene": outcome.get("visual_entry_scene"),
    "visual_last_scene": outcome.get("visual_last_scene"),
}, indent=2) + "\n", encoding="utf-8")

if missing_frames or missing_meta:
    missing = []
    if missing_frames:
        missing.append(f"missing frame files: {', '.join(missing_frames)}")
    if missing_meta:
        missing.append(f"missing meta files: {', '.join(missing_meta)}")
    raise SystemExit("capture incomplete: " + "; ".join(missing))

if timed_out and until_exit:
    raise SystemExit("capture timed out before scene exit")

cards = []
for png_path in png_files:
    cards.append(
        f'<figure class="card" id="{png_path.stem}">'
        f'<figcaption><a href="#{png_path.stem}">{png_path.name}</a></figcaption>'
        f'<img src="frames-png/{png_path.name}" alt="{png_path.name}">'
        f'<div class="path">frames-png/{png_path.name}</div>'
        f'</figure>'
    )

review_html = f"""<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>{ads_name} {tag} Host Review</title>
  <style>
    body {{ margin: 0; font: 14px/1.4 monospace; background: #0b0f14; color: #e6edf3; }}
    main {{ max-width: 1200px; margin: 0 auto; padding: 24px; }}
    .top {{ position: sticky; top: 0; background: #0b0f14; border-bottom: 1px solid #223; padding-bottom: 12px; margin-bottom: 20px; }}
    a {{ color: #8bd5ff; text-decoration: none; }}
    .card {{ margin: 0 0 24px 0; background: #111826; border: 1px solid #223; border-radius: 10px; overflow: hidden; }}
    figcaption {{ padding: 10px 12px; border-bottom: 1px solid #223; }}
    img {{ display: block; width: 100%; height: auto; image-rendering: pixelated; background: #000; }}
    .path {{ padding: 10px 12px; color: #9fb0c0; font-size: 12px; word-break: break-all; }}
  </style>
</head>
<body>
  <main>
    <div class="top">
      <h1>{ads_name} {tag} Host Capture Review</h1>
      <div>Boot: <code>{boot_string}</code></div>
      <div>Frames: {len(png_files)} at {requested_interval}-frame interval from {requested_start_frame} through {requested_frames}</div>
      <div>Frame metadata: <code>frame-meta/</code> overlay={str(capture_overlay).lower()}</div>
    </div>
    {''.join(cards)}
  </main>
</body>
</html>
"""
(output_dir / "review.html").write_text(review_html, encoding="utf-8")
print(json.dumps(result, indent=2))
PY
