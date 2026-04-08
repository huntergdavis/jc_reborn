#!/bin/bash
# Capture reference frames from all scenes for visual detection calibration.
#
# Runs the PS1 game through the headless DuckStation regtest emulator for each
# scene, capturing frames at regular intervals.  Builds a labeled reference
# dataset under regtest-references/.
#
# Usage:
#   ./scripts/capture-reference-frames.sh                    # all verified scenes
#   ./scripts/capture-reference-frames.sh --scene "STAND 1"  # single scene
#   ./scripts/capture-reference-frames.sh --all              # all 63 scenes
#   ./scripts/capture-reference-frames.sh --title-only       # title screen only
#   ./scripts/capture-reference-frames.sh --specials-only    # title + transition screens
#
# Output: regtest-references/<ads-name>-<tag>/frame_NNNNN.png
#         regtest-references/<ads-name>-<tag>/metadata.json

set -euo pipefail

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"
# shellcheck source=./docker-common.sh
source "$PROJECT_ROOT/scripts/docker-common.sh"

# Load shared config
# shellcheck source=../config/ps1/regtest-config.sh
source "$PROJECT_ROOT/config/ps1/regtest-config.sh"

# ── Defaults ─────────────────────────────────────────────────────────────────

REFERENCE_DIR="$PROJECT_ROOT/regtest-references"
SCENE_LIST_FILE="$PROJECT_ROOT/config/ps1/regtest-scenes.txt"
BOOTMODE_FILE="$PROJECT_ROOT/config/ps1/BOOTMODE.TXT"

# Capture settings: 1800 frames (30 sec), capture every 30 frames (2 per sec)
CAPTURE_FRAMES=1800
START_FRAME=""
START_FRAME_EXPLICIT=0
MIN_TAIL_FRAMES="${REGTEST_SCENE_CAPTURE_MIN_TAIL_FRAMES:-1200}"
CAPTURE_INTERVAL=30

# Title screen: 600 frames (10 sec)
TITLE_FRAMES=600
TITLE_INTERVAL=30

# Mode
MODE=""            # "" = verified-only, "all", "single", "title-only", "specials-only"
SINGLE_SCENE=""
SKIP_SPECIALS=0
SKIP_SCENES=0
PARALLEL="${REGTEST_PARALLEL:-2}"
DRY_RUN=0
SEED="${REGTEST_SEED:-1}"

# ── Argument Parsing ─────────────────────────────────────────────────────────

usage() {
    cat <<'USAGE'
Usage: capture-reference-frames.sh [OPTIONS]

Capture reference frames from PS1 scenes for visual detection calibration.

Options:
  --scene "ADS TAG"    Capture a single scene (e.g., "STAND 1")
  --all                Capture all 63 scenes (not just verified)
  --verified-only      Only capture verified scenes (default)
  --title-only         Only capture title screen
  --specials-only      Only capture special screens (title, transitions)
  --skip-specials      Skip special screen captures (title, transitions)
  --skip-scenes        Skip scene captures (only do specials)
  --frames N           Frames per scene (default: 1800 = 30 sec)
  --start-frame N      First PS1 frame to keep for scene captures
  --interval N         Capture every Nth frame (default: 30 = 2/sec)
  --seed N             Force deterministic BOOTMODE RNG seed (default: REGTEST_SEED or 1)
  --parallel N         Max parallel scene captures (default: 2)
  --dry-run            Show what would be done without running
  --output DIR         Override output directory (default: regtest-references/)
  -h, --help           Show this help

Output:
  regtest-references/<ads-name>-<tag>/frame_NNNNN.png   Scene frames
  regtest-references/<ads-name>-<tag>/metadata.json     Scene metadata
  regtest-references/title/frame_NNNNN.png              Title screen frames
  regtest-references/index.json                         Full reference index
USAGE
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --scene)         MODE="single"; SINGLE_SCENE="$2"; shift 2 ;;
        --all)           MODE="all"; shift ;;
        --verified-only) MODE=""; shift ;;
        --title-only)    MODE="title-only"; shift ;;
        --specials-only) MODE="specials-only"; shift ;;
        --skip-specials) SKIP_SPECIALS=1; shift ;;
        --skip-scenes)   SKIP_SCENES=1; shift ;;
        --frames)        CAPTURE_FRAMES="$2"; shift 2 ;;
        --start-frame)   START_FRAME="$2"; START_FRAME_EXPLICIT=1; shift 2 ;;
        --interval)      CAPTURE_INTERVAL="$2"; shift 2 ;;
        --seed)          SEED="$2"; shift 2 ;;
        --parallel)      PARALLEL="$2"; shift 2 ;;
        --dry-run)       DRY_RUN=1; shift ;;
        --output)        REFERENCE_DIR="$2"; shift 2 ;;
        -h|--help)       usage ;;
        *)               echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

# ── Prerequisite Checks ─────────────────────────────────────────────────────

# Check for CD image
if [ ! -f "$PROJECT_ROOT/jcreborn.cue" ]; then
    echo "ERROR: jcreborn.cue not found in project root." >&2
    echo "" >&2
    echo "The CD image must be built first. Run:" >&2
    echo "  ./scripts/make-cd-image.sh" >&2
    echo "" >&2
    echo "Or if you need to rebuild the PS1 binary first:" >&2
    echo "  ./scripts/rebuild-and-let-run.sh noclean" >&2
    exit 1
fi

# Check for regtest capability
if ! command -v duckstation-regtest >/dev/null 2>&1; then
    if ! docker_maybe_init || ! "${DOCKER_CMD[@]}" image inspect "jc-reborn-regtest:latest" >/dev/null 2>&1; then
        echo "ERROR: No regtest runner available." >&2
        echo "Need either duckstation-regtest in PATH or Docker image jc-reborn-regtest:latest." >&2
        echo "Build the Docker image with: ./scripts/build-regtest-image.sh" >&2
        exit 2
    fi
fi

# Check for scene list
if [ ! -f "$SCENE_LIST_FILE" ]; then
    echo "ERROR: Scene list not found: $SCENE_LIST_FILE" >&2
    exit 1
fi

mkdir -p "$REFERENCE_DIR"

log() {
    echo "[capture-ref] $*" >&2
}

# ── BOOTMODE Helpers ─────────────────────────────────────────────────────────

BOOTMODE_BACKUP=""
REGTEST_BUILD_LOCK="$PROJECT_ROOT/.regtest-build.lock"

stage_bootmode() {
    local boot_string="$1"
    if [ -f "$BOOTMODE_FILE" ]; then
        BOOTMODE_BACKUP="$(mktemp /tmp/capture-ref-bootmode-XXXXXX.txt)"
        cp "$BOOTMODE_FILE" "$BOOTMODE_BACKUP"
    fi
    printf '%s\n' "$boot_string" > "$BOOTMODE_FILE"
}

restore_bootmode() {
    if [ -n "${BOOTMODE_BACKUP:-}" ] && [ -f "$BOOTMODE_BACKUP" ]; then
        cp "$BOOTMODE_BACKUP" "$BOOTMODE_FILE"
        rm -f "$BOOTMODE_BACKUP"
        BOOTMODE_BACKUP=""
    elif [ -f "$BOOTMODE_FILE" ]; then
        : > "$BOOTMODE_FILE"
    fi
}

cleanup() {
    restore_bootmode
}
trap cleanup EXIT

# ── Scene Capture ────────────────────────────────────────────────────────────

capture_scene() {
    # Args: ADS_NAME TAG SCENE_INDEX STATUS BOOT_STRING
    local ads_name="$1"
    local tag="$2"
    local scene_index="$3"
    local status="$4"
    local boot_string="$5"
    local scene_label="${ads_name}-${tag}"
    local scene_dir="$REFERENCE_DIR/$scene_label"
    local scene_start_frame="$START_FRAME"
    local scene_capture_frames="$CAPTURE_FRAMES"
    local capture_ts
    capture_ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

    if [ -z "$scene_start_frame" ]; then
        scene_start_frame="$(python3 "$SCRIPT_DIR/get-scene-capture-start.py" --scene "$ads_name $tag")"
    fi
    if ! [[ "$scene_start_frame" =~ ^[0-9]+$ ]]; then
        echo "ERROR: scene start frame must be an integer >= 0" >&2
        return 1
    fi
    if [ "$scene_start_frame" -lt 0 ]; then
        echo "ERROR: scene start frame must be >= 0" >&2
        return 1
    fi
    if [ "$START_FRAME_EXPLICIT" -eq 0 ]; then
        min_frames=$((scene_start_frame + MIN_TAIL_FRAMES))
        if [ "$scene_capture_frames" -lt "$min_frames" ]; then
            scene_capture_frames="$min_frames"
        fi
    fi
    if [ "$scene_capture_frames" -lt "$scene_start_frame" ]; then
        echo "ERROR: --frames must be >= resolved scene start frame" >&2
        return 1
    fi

    if [ "$DRY_RUN" -eq 1 ]; then
        echo "  [dry-run] Would capture: $ads_name tag $tag (index=$scene_index) => $scene_dir"
        echo "            Boot: $boot_string"
        echo "            Frames: $scene_capture_frames, Start: $scene_start_frame, Interval: $CAPTURE_INTERVAL, Seed: $SEED"
        return 0
    fi

    log "Capturing $ads_name tag $tag (index=$scene_index) ..."

    mkdir -p "$scene_dir"

    # Use regtest-scene.sh which handles BOOTMODE, CD rebuild, snapshot, and regtest
    local scene_result
    scene_result=0
    "$SCRIPT_DIR/regtest-scene.sh" \
        --scene "$ads_name $tag" \
        --scene-index "$scene_index" \
        --status "$status" \
        --boot "$boot_string" \
        --frames "$scene_capture_frames" \
        --start-frame "$scene_start_frame" \
        --interval "$CAPTURE_INTERVAL" \
        --seed "$SEED" \
        --output "$scene_dir/.regtest-work" \
        --quiet \
        > "$scene_dir/.regtest-result.json" 2>"$scene_dir/.regtest-stderr.log" \
        || scene_result=$?

    # Move captured frames from the filtered regtest output into the scene reference dir
    local frames_found=0
    local frames_src=""

    if [ -f "$scene_dir/.regtest-result.json" ]; then
        frames_src="$(python3 - <<'PY' "$scene_dir/.regtest-result.json"
import json, sys
from pathlib import Path

payload = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
print((payload.get("paths") or {}).get("frames_dir") or "")
PY
)"
    fi
    if [ -z "$frames_src" ] || [ ! -d "$frames_src" ]; then
        # Fallback for older result formats.
        if [ -d "$scene_dir/.regtest-work/frames" ]; then
            frames_src="$scene_dir/.regtest-work/frames"
        else
            frames_src="$(find "$scene_dir/.regtest-work" -type d -name "frames" 2>/dev/null | head -1)"
        fi
    fi

    if [ -n "$frames_src" ] && [ -d "$frames_src" ]; then
        # Find and copy PNG frames. DuckStation may nest them under a game subdir.
        while IFS= read -r -d '' png; do
            local basename
            basename="$(basename "$png")"
            cp "$png" "$scene_dir/$basename"
            frames_found=$((frames_found + 1))
        done < <(find "$frames_src" -type f -name "*.png" -print0 2>/dev/null | sort -z)
    fi

    # Write metadata sidecar
    SCENE_CAPTURE_FRAMES="$scene_capture_frames" SCENE_START_FRAME="$scene_start_frame" SEED="$SEED" \
    python3 - "$scene_dir" "$ads_name" "$tag" "$scene_index" "$status" \
              "$boot_string" "$frames_found" "$capture_ts" "$scene_result" <<'PYEOF'
import json, sys, os, glob

scene_dir = sys.argv[1]
ads_name = sys.argv[2]
tag = int(sys.argv[3])
scene_index = int(sys.argv[4]) if sys.argv[4] else None
status = sys.argv[5]
boot_string = sys.argv[6]
frames_found = int(sys.argv[7])
capture_ts = sys.argv[8]
scene_result = int(sys.argv[9])

# List actual frame files
frame_files = sorted(
    os.path.basename(f) for f in glob.glob(os.path.join(scene_dir, "frame_*.png"))
)

metadata = {
    "ads_name": ads_name,
    "ads_file": f"{ads_name}.ADS",
    "tag": tag,
    "scene_index": scene_index,
    "status": status,
    "boot_string": boot_string,
    "seed": int(os.environ.get("SEED", 1)),
    "capture_frames": int(os.environ.get("SCENE_CAPTURE_FRAMES", os.environ.get("CAPTURE_FRAMES", 1800))),
    "capture_start_frame": int(os.environ.get("SCENE_START_FRAME", os.environ.get("START_FRAME", 0))),
    "capture_interval": int(os.environ.get("CAPTURE_INTERVAL", 30)),
    "frame_count": len(frame_files),
    "frames": frame_files,
    "capture_date": capture_ts,
    "regtest_exit_code": scene_result,
}

with open(os.path.join(scene_dir, "metadata.json"), "w") as f:
    json.dump(metadata, f, indent=2)
    f.write("\n")
PYEOF

    telemetry_copy=""
    printf_copy=""
    raw_hashes_copy=""
    build_log_copy=""
    if [ -f "$scene_dir/.regtest-work/telemetry.json" ]; then
        telemetry_copy="$scene_dir/telemetry.json"
        cp "$scene_dir/.regtest-work/telemetry.json" "$telemetry_copy"
    fi
    if [ -f "$scene_dir/.regtest-work/printf.log" ]; then
        printf_copy="$scene_dir/printf.log"
        cp "$scene_dir/.regtest-work/printf.log" "$printf_copy"
    elif [ -f "$scene_dir/.regtest-work/tty-output.txt" ]; then
        printf_copy="$scene_dir/printf.log"
        cp "$scene_dir/.regtest-work/tty-output.txt" "$printf_copy"
    fi
    if [ -f "$scene_dir/.regtest-work/raw-hashes.json" ]; then
        raw_hashes_copy="$scene_dir/raw-hashes.json"
        cp "$scene_dir/.regtest-work/raw-hashes.json" "$raw_hashes_copy"
    fi
    if [ -f "$scene_dir/.regtest-work/build.log" ]; then
        build_log_copy="$scene_dir/build.log"
        cp "$scene_dir/.regtest-work/build.log" "$build_log_copy"
    fi

    if [ -f "$scene_dir/.regtest-result.json" ]; then
        python3 - "$scene_dir/.regtest-result.json" "$scene_dir/result.json" "$scene_dir" "$capture_ts" "$telemetry_copy" "$printf_copy" "$raw_hashes_copy" "$build_log_copy" <<'PYEOF'
import json, os, sys
from pathlib import Path

source_path = Path(sys.argv[1])
dest_path = Path(sys.argv[2])
scene_dir = Path(sys.argv[3]).resolve()
capture_date = sys.argv[4]
telemetry_copy = sys.argv[5]
printf_copy = sys.argv[6]
raw_hashes_copy = sys.argv[7]
build_log_copy = sys.argv[8]

payload = json.loads(source_path.read_text(encoding="utf-8"))
payload["capture_date"] = capture_date
paths = payload.setdefault("paths", {})
paths["output_dir"] = str(scene_dir)
paths["frames_dir"] = str(scene_dir)
if telemetry_copy:
    paths["telemetry"] = telemetry_copy
else:
    paths.pop("telemetry", None)
if printf_copy:
    paths["printf_log"] = printf_copy
else:
    paths.pop("printf_log", None)
if raw_hashes_copy:
    paths["raw_hashes"] = raw_hashes_copy
else:
    paths.pop("raw_hashes", None)
if build_log_copy:
    paths["build_log"] = build_log_copy
else:
    paths.pop("build_log", None)
payload.setdefault("config", {})
payload["config"]["frames_dir_layout"] = "flat"
dest_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
PYEOF
    fi

    # Clean up work directory (keep only frames and metadata)
    if [ -d "$scene_dir/.regtest-work" ]; then
        chmod -R u+w "$scene_dir/.regtest-work" 2>/dev/null || true
        "${DOCKER_CMD[@]}" run --rm --platform linux/amd64 \
            -v "$scene_dir/.regtest-work":/work \
            jc-reborn-regtest:latest \
            bash -lc "chown -R $(id -u):$(id -g) /work 2>/dev/null || true" \
            >/dev/null 2>&1 || true
        rm -rf "$scene_dir/.regtest-work" >/dev/null 2>&1 || true
    fi
    rm -f "$scene_dir/.regtest-result.json" "$scene_dir/.regtest-stderr.log"

    if [ "$frames_found" -eq 0 ]; then
        log "WARNING: No frames captured for $ads_name tag $tag (exit=$scene_result)"
        return 1
    else
        log "  => $frames_found frames saved to $scene_dir/"
        return 0
    fi
}

export CAPTURE_FRAMES CAPTURE_INTERVAL START_FRAME

# ── Special Screen Capture ───────────────────────────────────────────────────

capture_title_screen() {
    local title_dir="$REFERENCE_DIR/title"
    local capture_ts
    capture_ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

    if [ "$DRY_RUN" -eq 1 ]; then
        echo "  [dry-run] Would capture title screen => $title_dir"
        echo "            Boot: story (normal boot)"
        echo "            Frames: $TITLE_FRAMES, Interval: $TITLE_INTERVAL"
        return 0
    fi

    log "Capturing title screen (normal boot, $TITLE_FRAMES frames) ..."

    mkdir -p "$title_dir"

    # Boot normally — "story" mode shows the title screen
    local result=0
    (
        flock 9
        stage_bootmode "story"
        "$SCRIPT_DIR/make-cd-image.sh" > "$title_dir/.build.log" 2>&1
        restore_bootmode
    ) 9>"$REGTEST_BUILD_LOCK"

    # Run regtest for title screen duration
    "$SCRIPT_DIR/run-regtest.sh" \
        --frames "$TITLE_FRAMES" \
        --dumpinterval "$TITLE_INTERVAL" \
        --dumpdir "$title_dir/.regtest-work" \
        --timeout 60 \
        > "$title_dir/.regtest.log" 2>&1 || result=$?

    # Collect frames
    local frames_found=0
    local frames_src
    frames_src="$(find "$title_dir/.regtest-work" -type d -name "frames" 2>/dev/null | head -1)"

    if [ -n "$frames_src" ] && [ -d "$frames_src" ]; then
        while IFS= read -r -d '' png; do
            cp "$png" "$title_dir/$(basename "$png")"
            frames_found=$((frames_found + 1))
        done < <(find "$frames_src" -type f -name "*.png" -print0 2>/dev/null | sort -z)
    fi

    # Write metadata
    python3 - "$title_dir" "$frames_found" "$capture_ts" "$result" "$TITLE_FRAMES" "$TITLE_INTERVAL" <<'PYEOF'
import json, sys, os, glob

title_dir = sys.argv[1]
frames_found = int(sys.argv[2])
capture_ts = sys.argv[3]
result = int(sys.argv[4])
cap_frames = int(sys.argv[5])
cap_interval = int(sys.argv[6])

frame_files = sorted(
    os.path.basename(f) for f in glob.glob(os.path.join(title_dir, "frame_*.png"))
)

metadata = {
    "screen_type": "title",
    "description": "Title screen captured during normal story boot",
    "boot_string": "story",
    "capture_frames": cap_frames,
    "capture_interval": cap_interval,
    "frame_count": len(frame_files),
    "frames": frame_files,
    "capture_date": capture_ts,
    "regtest_exit_code": result,
}

with open(os.path.join(title_dir, "metadata.json"), "w") as f:
    json.dump(metadata, f, indent=2)
    f.write("\n")
PYEOF

    rm -rf "$title_dir/.regtest-work" "$title_dir/.regtest.log" "$title_dir/.build.log"

    if [ "$frames_found" -eq 0 ]; then
        log "WARNING: No frames captured for title screen (exit=$result)"
    else
        log "  => $frames_found title screen frames saved to $title_dir/"
    fi
}

capture_transition_screens() {
    # Ocean and black/fade transitions happen during normal story boot.
    # We capture a longer run (3600 frames = 60 sec) to catch the transition
    # from title screen into the first story scene.
    local transition_dir="$REFERENCE_DIR/transitions"
    local capture_ts
    capture_ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

    if [ "$DRY_RUN" -eq 1 ]; then
        echo "  [dry-run] Would capture transition screens => $transition_dir"
        echo "            Boot: story (normal boot, longer run for transitions)"
        echo "            Frames: 3600, Interval: $TITLE_INTERVAL"
        return 0
    fi

    log "Capturing transition screens (3600 frames = 60 sec) ..."

    mkdir -p "$transition_dir"

    local result=0
    (
        flock 9
        stage_bootmode "story"
        "$SCRIPT_DIR/make-cd-image.sh" > "$transition_dir/.build.log" 2>&1
        restore_bootmode
    ) 9>"$REGTEST_BUILD_LOCK"

    "$SCRIPT_DIR/run-regtest.sh" \
        --frames 3600 \
        --dumpinterval "$TITLE_INTERVAL" \
        --dumpdir "$transition_dir/.regtest-work" \
        --timeout 120 \
        > "$transition_dir/.regtest.log" 2>&1 || result=$?

    # Collect frames
    local frames_found=0
    local frames_src
    frames_src="$(find "$transition_dir/.regtest-work" -type d -name "frames" 2>/dev/null | head -1)"

    if [ -n "$frames_src" ] && [ -d "$frames_src" ]; then
        while IFS= read -r -d '' png; do
            cp "$png" "$transition_dir/$(basename "$png")"
            frames_found=$((frames_found + 1))
        done < <(find "$frames_src" -type f -name "*.png" -print0 2>/dev/null | sort -z)
    fi

    # Write metadata
    python3 - "$transition_dir" "$frames_found" "$capture_ts" "$result" <<'PYEOF'
import json, sys, os, glob

tdir = sys.argv[1]
frames_found = int(sys.argv[2])
capture_ts = sys.argv[3]
result = int(sys.argv[4])

frame_files = sorted(
    os.path.basename(f) for f in glob.glob(os.path.join(tdir, "frame_*.png"))
)

metadata = {
    "screen_type": "transitions",
    "description": "Title-to-scene transition frames (ocean, black/fade) from normal story boot",
    "boot_string": "story",
    "capture_frames": 3600,
    "capture_interval": 30,
    "frame_count": len(frame_files),
    "frames": frame_files,
    "capture_date": capture_ts,
    "regtest_exit_code": result,
    "notes": "Frames cover ~60 sec of normal boot, including title screen, "
             "ocean/pan transition, and first scene entry. Classify frames "
             "into title/ocean/black categories using calibrate-visual-detect.py."
}

with open(os.path.join(tdir, "metadata.json"), "w") as f:
    json.dump(metadata, f, indent=2)
    f.write("\n")
PYEOF

    rm -rf "$transition_dir/.regtest-work" "$transition_dir/.regtest.log" "$transition_dir/.build.log"

    if [ "$frames_found" -eq 0 ]; then
        log "WARNING: No frames captured for transitions (exit=$result)"
    else
        log "  => $frames_found transition frames saved to $transition_dir/"
    fi
}

# ── Parse Scene List ─────────────────────────────────────────────────────────

declare -a ALL_ADS=()
declare -a ALL_TAGS=()
declare -a ALL_INDEXES=()
declare -a ALL_STATUSES=()
declare -a ALL_BOOTS=()

parse_scene_list() {
    while IFS= read -r line; do
        line="${line%%#*}"
        line="$(echo "$line" | xargs)"
        [ -z "$line" ] && continue

        read -r ads_name tag scene_index status rest <<< "$line"
        [ -z "$ads_name" ] || [ -z "$tag" ] && continue

        ALL_ADS+=("$ads_name")
        ALL_TAGS+=("$tag")
        ALL_INDEXES+=("$scene_index")
        ALL_STATUSES+=("$status")
        ALL_BOOTS+=("$rest")
    done < "$SCENE_LIST_FILE"
}

parse_scene_list

# ── Main Dispatch ────────────────────────────────────────────────────────────

echo "========================================"
echo "PS1 Reference Frame Capture"
echo "========================================"
echo ""
echo "Output:    $REFERENCE_DIR"
echo "Scenes:    ${#ALL_ADS[@]} total in scene list"
echo "Frames:    $CAPTURE_FRAMES per scene (interval=$CAPTURE_INTERVAL)"
echo "Mode:      ${MODE:-verified-only}"
echo ""

TOTAL_SCENES=0
PASS_SCENES=0
FAIL_SCENES=0

run_scene_captures() {
    local pids=()
    local labels=()
    local running=0

    wait_for_slot() {
        while [ "$running" -ge "$PARALLEL" ]; do
            for i in "${!pids[@]}"; do
                if ! kill -0 "${pids[$i]}" 2>/dev/null; then
                    wait "${pids[$i]}" 2>/dev/null || true
                    unset 'pids[i]'
                    running=$((running - 1))
                fi
            done
            if [ "$running" -ge "$PARALLEL" ]; then
                sleep 0.5
            fi
        done
    }

    for idx in "${!ALL_ADS[@]}"; do
        local ads_name="${ALL_ADS[$idx]}"
        local tag="${ALL_TAGS[$idx]}"
        local scene_index="${ALL_INDEXES[$idx]}"
        local status="${ALL_STATUSES[$idx]}"
        local boot_string="${ALL_BOOTS[$idx]}"

        # Apply filters
        if [ "$MODE" = "single" ]; then
            local wanted_ads wanted_tag
            read -r wanted_ads wanted_tag <<< "$SINGLE_SCENE"
            if [ "$ads_name" != "$wanted_ads" ] || [ "$tag" != "$wanted_tag" ]; then
                continue
            fi
        elif [ "$MODE" = "all" ]; then
            : # include everything
        else
            # Default: verified only
            if [ "$status" != "verified" ]; then
                continue
            fi
        fi

        # Default boot string if missing
        if [ -z "$boot_string" ]; then
            boot_string="story scene $scene_index"
        fi

        TOTAL_SCENES=$((TOTAL_SCENES + 1))

        # Check if already captured (skip unless --force)
        local scene_dir="$REFERENCE_DIR/${ads_name}-${tag}"
        if [ -f "$scene_dir/metadata.json" ] && [ "$DRY_RUN" -eq 0 ]; then
            local existing_count
            existing_count="$(python3 -c "import json; print(json.load(open('$scene_dir/metadata.json'))['frame_count'])" 2>/dev/null || echo 0)"
            if [ "$existing_count" -gt 0 ]; then
                log "Skipping $ads_name tag $tag (already captured, $existing_count frames)"
                PASS_SCENES=$((PASS_SCENES + 1))
                continue
            fi
        fi

        if [ "$DRY_RUN" -eq 1 ] || [ "$PARALLEL" -le 1 ]; then
            # Sequential mode or dry run
            if capture_scene "$ads_name" "$tag" "$scene_index" "$status" "$boot_string"; then
                PASS_SCENES=$((PASS_SCENES + 1))
            else
                FAIL_SCENES=$((FAIL_SCENES + 1))
            fi
        else
            wait_for_slot

            (
                capture_scene "$ads_name" "$tag" "$scene_index" "$status" "$boot_string"
            ) &
            pids+=($!)
            labels+=("$ads_name $tag")
            running=$((running + 1))
        fi
    done

    # Wait for remaining parallel jobs
    for pid in "${pids[@]}"; do
        wait "$pid" 2>/dev/null && PASS_SCENES=$((PASS_SCENES + 1)) || FAIL_SCENES=$((FAIL_SCENES + 1))
    done
}

# Title-only mode
if [ "$MODE" = "title-only" ]; then
    capture_title_screen
    echo ""
    echo "Done. Run ./scripts/build-reference-index.py to update the index."
    exit 0
fi

# Specials-only mode
if [ "$MODE" = "specials-only" ]; then
    echo "--- Special Screens ---"
    capture_title_screen
    capture_transition_screens
    echo ""
    echo "Done. Run ./scripts/build-reference-index.py to update the index."
    exit 0
fi

# Scene captures
if [ "$SKIP_SCENES" -eq 0 ]; then
    echo "--- Scene Captures ---"
    run_scene_captures
    echo ""
fi

# Special screen captures (unless skipped or single-scene mode)
if [ "$SKIP_SPECIALS" -eq 0 ] && [ "$MODE" != "single" ]; then
    echo "--- Special Screens ---"
    capture_title_screen
    capture_transition_screens
    echo ""
fi

# ── Summary ──────────────────────────────────────────────────────────────────

echo "========================================"
echo "Capture Summary"
echo "========================================"
echo ""
echo "Scenes captured:  $PASS_SCENES / $TOTAL_SCENES"
if [ "$FAIL_SCENES" -gt 0 ]; then
    echo "Failed:           $FAIL_SCENES"
fi
echo "Output:           $REFERENCE_DIR/"
echo ""
echo "Next steps:"
echo "  1. Review captured frames in $REFERENCE_DIR/"
echo "  2. Run ./scripts/build-reference-index.py to build the index"
echo "  3. Run ./scripts/calibrate-visual-detect.py to extract color calibration"
echo ""

if [ "$FAIL_SCENES" -gt 0 ]; then
    exit 1
fi
exit 0
