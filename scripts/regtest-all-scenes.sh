#!/bin/bash
# regtest-all-scenes.sh — Run multiple PS1 scenes through duckstation-regtest
#                         in parallel and produce a summary report.
#
# Usage:
#   ./scripts/regtest-all-scenes.sh --verified-only --output results/
#   ./scripts/regtest-all-scenes.sh --all --parallel 8
#   ./scripts/regtest-all-scenes.sh --status verified,bringup
#
# Reads scene list from config/ps1/regtest-scenes.txt (or override with
# --scene-list <file>).  Runs regtest-scene.sh for each scene, N at a time.
#
# Scene list format:
#   ADS_NAME TAG SCENE_INDEX STATUS [BOOTMODE...]
# Example:
#   STAND 1 38 verified story scene 38
# If BOOTMODE is omitted, regtest-scene.sh derives one from SCENE_INDEX or
# falls back to "island ads ADS_NAME TAG".

set -euo pipefail

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# Load shared config
# shellcheck source=../config/ps1/regtest-config.sh
source "$PROJECT_ROOT/config/ps1/regtest-config.sh"

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------
PARALLEL="$REGTEST_PARALLEL"
OUTPUT_ROOT="$REGTEST_OUTPUT_DIR"
SCENE_LIST_FILE="$REGTEST_SCENE_LIST"
FRAMES="$REGTEST_FRAMES"
START_FRAME=""
START_FRAME_EXPLICIT=0
MIN_TAIL_FRAMES="${REGTEST_SCENE_CAPTURE_MIN_TAIL_FRAMES:-1200}"
INTERVAL="$REGTEST_INTERVAL"
STATUS_FILTER=""       # empty = use mode flag
MODE=""                # verified-only | all | (default = verified + bringup)
EXTRA_ARGS=()

usage() {
    cat <<'USAGE'
Usage: regtest-all-scenes.sh [OPTIONS]

Options:
  --verified-only      Only run scenes with status=verified
  --all                Run all scenes including blocked/untested (63 scenes)
  --status STATUSES    Comma-separated status filter (e.g. "verified,bringup")
  --parallel N         Max concurrent tests (default: 4)
  --frames N           Frames per scene (default: 1800)
  --start-frame N      First PS1 frame to keep for every scene (default: per-scene reviewed start)
  --interval N         Capture interval (default: 60)
  --scene-list FILE    Scene list file (default: config/ps1/regtest-scenes.txt)
  --output DIR         Output root directory (default: regtest-results)
  --skip-build         Pass --skip-build to each scene test
  -h, --help           Show this help
USAGE
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --verified-only)  MODE="verified-only"; shift ;;
        --all)            MODE="all"; shift ;;
        --status)         STATUS_FILTER="$2"; shift 2 ;;
        --parallel)       PARALLEL="$2"; shift 2 ;;
        --frames)         FRAMES="$2"; shift 2 ;;
        --start-frame)    START_FRAME="$2"; START_FRAME_EXPLICIT=1; shift 2 ;;
        --interval)       INTERVAL="$2"; shift 2 ;;
        --scene-list)     SCENE_LIST_FILE="$2"; shift 2 ;;
        --output)         OUTPUT_ROOT="$2"; shift 2 ;;
        --skip-build)     EXTRA_ARGS+=(--skip-build); shift ;;
        -h|--help)        usage ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

# Resolve status filter from mode
if [ -n "$STATUS_FILTER" ]; then
    : # user override
elif [ "$MODE" = "verified-only" ]; then
    STATUS_FILTER="verified"
elif [ "$MODE" = "all" ]; then
    STATUS_FILTER="verified,bringup,blocked,untested"
else
    # default: verified + bringup
    STATUS_FILTER="verified,bringup"
fi

# ---------------------------------------------------------------------------
# Check prerequisites
# ---------------------------------------------------------------------------
if ! command -v "$REGTEST_BIN" >/dev/null 2>&1; then
    cat >&2 <<EOF
ERROR: '$REGTEST_BIN' not found in PATH.

To build the headless regtest binary, see:
  https://github.com/stenzek/duckstation/blob/master/README.md

Set REGTEST_BIN=/path/to/duckstation-regtest to override.
EOF
    exit 2
fi

if [ ! -f "$SCENE_LIST_FILE" ]; then
    echo "ERROR: Scene list not found: $SCENE_LIST_FILE" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Parse scene list
# ---------------------------------------------------------------------------
declare -a SCENES=()        # "ADS_NAME TAG"
declare -a SCENE_INDEXES=() # scene_index or empty
declare -a STATUSES=()      # status per scene
declare -a BOOTSTRINGS=()   # explicit boot string or empty

# Build a lookup set from the filter
declare -A ALLOWED_STATUS=()
IFS=',' read -ra FILTER_PARTS <<< "$STATUS_FILTER"
for s in "${FILTER_PARTS[@]}"; do
    ALLOWED_STATUS["$(echo "$s" | tr -d ' ')"]="1"
done

while IFS= read -r line; do
    # Strip comments and trim
    line="${line%%#*}"
    line="$(echo "$line" | xargs)"
    [ -z "$line" ] && continue

    read -r ads_name tag scene_index status boot_a boot_b boot_c boot_d <<< "$line"
    [ -z "$ads_name" ] || [ -z "$tag" ] && continue

    if [ -z "$status" ]; then
        # Backward compatibility with older "ADS TAG STATUS" files.
        status="${scene_index:-untested}"
        scene_index=""
    fi

    boot_string=""
    if [ -n "${boot_a:-}" ]; then
        boot_string="$boot_a"
        [ -n "${boot_b:-}" ] && boot_string="$boot_string $boot_b"
        [ -n "${boot_c:-}" ] && boot_string="$boot_string $boot_c"
        [ -n "${boot_d:-}" ] && boot_string="$boot_string $boot_d"
    fi

    if [ -n "${ALLOWED_STATUS[$status]:-}" ]; then
        SCENES+=("$ads_name $tag")
        SCENE_INDEXES+=("$scene_index")
        STATUSES+=("$status")
        BOOTSTRINGS+=("$boot_string")
    fi
done < "$SCENE_LIST_FILE"

TOTAL=${#SCENES[@]}
if [ "$TOTAL" -eq 0 ]; then
    echo "No scenes matched filter: $STATUS_FILTER" >&2
    exit 0
fi

echo "========================================"
echo "PS1 Regtest — $TOTAL scene(s), $PARALLEL parallel"
echo "Filter: $STATUS_FILTER"
echo "Output: $OUTPUT_ROOT"
echo "Frames: $FRAMES  Start: ${START_FRAME:-per-scene}  Interval: $INTERVAL"
echo "========================================"
echo ""

# ---------------------------------------------------------------------------
# Create output root with timestamp
# ---------------------------------------------------------------------------
RUN_ID="$(date +%Y%m%d-%H%M%S)"
RUN_DIR="$OUTPUT_ROOT/run-$RUN_ID"
mkdir -p "$RUN_DIR"

# ---------------------------------------------------------------------------
# Run scenes in parallel using a job-slot semaphore
# ---------------------------------------------------------------------------
PIDS=()
SCENE_DIRS=()
SCENE_LABELS=()
RUNNING=0

wait_for_slot() {
    while [ "$RUNNING" -ge "$PARALLEL" ]; do
        # Wait for any child to finish
        for i in "${!PIDS[@]}"; do
            if ! kill -0 "${PIDS[$i]}" 2>/dev/null; then
                wait "${PIDS[$i]}" 2>/dev/null || true
                unset 'PIDS[i]'
                RUNNING=$((RUNNING - 1))
            fi
        done
        if [ "$RUNNING" -ge "$PARALLEL" ]; then
            sleep 0.5
        fi
    done
}

for idx in "${!SCENES[@]}"; do
    spec="${SCENES[$idx]}"
    read -r ads tag <<< "$spec"
    scene_index="${SCENE_INDEXES[$idx]}"
    status="${STATUSES[$idx]}"
    boot_string="${BOOTSTRINGS[$idx]}"
    scene_label="${ads} ${tag}"
    scene_dir="$RUN_DIR/${ads,,}-${tag}"
    scene_args=()
    scene_start_frame="$START_FRAME"
    scene_frames="$FRAMES"

    if [ -n "$scene_index" ]; then
        scene_args+=(--scene-index "$scene_index")
    fi
    if [ -n "$boot_string" ]; then
        scene_args+=(--boot "$boot_string")
    fi
    if [ -z "$scene_start_frame" ]; then
        scene_start_frame="$(python3 "$SCRIPT_DIR/get-scene-capture-start.py" --scene "$scene_label")"
    fi
    if [ "$START_FRAME_EXPLICIT" -eq 0 ]; then
        min_frames=$((scene_start_frame + MIN_TAIL_FRAMES))
        if [ "$scene_frames" -lt "$min_frames" ]; then
            scene_frames="$min_frames"
        fi
    fi

    wait_for_slot

    echo "[${idx}/${TOTAL}] Starting: $scene_label"

    "$SCRIPT_DIR/regtest-scene.sh" \
        --scene "$scene_label" \
        --status "$status" \
        "${scene_args[@]}" \
        --frames "$scene_frames" \
        --start-frame "$scene_start_frame" \
        --interval "$INTERVAL" \
        --output "$scene_dir" \
        --quiet \
        "${EXTRA_ARGS[@]}" \
        > "$scene_dir/stdout.json" 2>"$scene_dir/stderr.log" &

    PIDS+=($!)
    SCENE_DIRS+=("$scene_dir")
    SCENE_LABELS+=("$scene_label")
    RUNNING=$((RUNNING + 1))
done

# Wait for all remaining jobs
for pid in "${PIDS[@]}"; do
    wait "$pid" 2>/dev/null || true
done

echo ""
echo "All scenes completed. Collecting results..."
echo ""

# ---------------------------------------------------------------------------
# Collect results into summary
# ---------------------------------------------------------------------------
SUMMARY_FILE="$RUN_DIR/summary.json"
PASS=0
FAIL=0

# Table header
printf "%-20s %6s %8s %10s %s\n" "SCENE" "FRAMES" "STATUS" "DROPS" "NOTES"
printf "%-20s %6s %8s %10s %s\n" "--------------------" "------" "--------" "----------" "-----"

RESULTS_JSON="["
FIRST=1

for idx in "${!SCENE_LABELS[@]}"; do
    label="${SCENE_LABELS[$idx]}"
    dir="${SCENE_DIRS[$idx]}"
    result_file="$dir/result.json"

    if [ -f "$result_file" ]; then
        # Parse key fields from result.json
        exit_code=$(python3 -c "import json; r=json.load(open('$result_file')); print(r['outcome']['exit_code'])" 2>/dev/null || echo "?")
        frames_cap=$(python3 -c "import json; r=json.load(open('$result_file')); print(r['outcome']['frames_captured'])" 2>/dev/null || echo "0")
        has_fatal=$(python3 -c "import json; r=json.load(open('$result_file')); print(r['outcome']['has_fatal_error'])" 2>/dev/null || echo "False")
        timed_out=$(python3 -c "import json; r=json.load(open('$result_file')); print(r['outcome']['timed_out'])" 2>/dev/null || echo "False")

        # Determine status
        notes=""
        if [ "$timed_out" = "True" ]; then
            status="TIMEOUT"
            notes="killed after ${REGTEST_TIMEOUT}s"
            FAIL=$((FAIL + 1))
        elif [ "$has_fatal" = "True" ]; then
            status="CRASH"
            notes="fatalError in output"
            FAIL=$((FAIL + 1))
        elif [ "$frames_cap" = "0" ]; then
            status="FAIL"
            notes="no frames captured"
            FAIL=$((FAIL + 1))
        elif [ "$exit_code" != "0" ]; then
            status="FAIL"
            notes="exit=$exit_code"
            FAIL=$((FAIL + 1))
        else
            status="PASS"
            PASS=$((PASS + 1))
        fi

        # Get drop info
        drops=$(python3 -c "
import json
r = json.load(open('$result_file'))
d = r.get('outcome', {}).get('drop_indicators', {})
parts = [f'{k.replace(\"drop_\",\"\")}={v}' for k,v in d.items() if v > 0]
print(','.join(parts) if parts else '-')
" 2>/dev/null || echo "-")

        printf "%-20s %6s %8s %10s %s\n" "$label" "$frames_cap" "$status" "$drops" "$notes"

        # Append to JSON array
        if [ "$FIRST" -eq 0 ]; then RESULTS_JSON+=","; fi
        FIRST=0
        RESULTS_JSON+="$(cat "$result_file")"
    else
        printf "%-20s %6s %8s %10s %s\n" "$label" "?" "ERROR" "-" "no result.json"
        FAIL=$((FAIL + 1))

        if [ "$FIRST" -eq 0 ]; then RESULTS_JSON+=","; fi
        FIRST=0
        read -r ads tag <<< "$label"
        RESULTS_JSON+="{\"scene\":{\"ads_name\":\"$ads\",\"tag\":$tag},\"outcome\":{\"exit_code\":-1,\"error\":\"no result\"}}"
    fi
done

RESULTS_JSON+="]"

# Write summary JSON
python3 -c "
import json, sys
results = json.loads(sys.stdin.read())
summary = {
    'run_id': '$RUN_ID',
    'run_dir': '$RUN_DIR',
    'filter': '$STATUS_FILTER',
    'total': $TOTAL,
    'passed': $PASS,
    'failed': $FAIL,
    'scenes': results,
}
json.dump(summary, open('$SUMMARY_FILE', 'w'), indent=2)
print()
" <<< "$RESULTS_JSON"

echo ""
echo "========================================"
echo "Summary: $PASS passed, $FAIL failed (of $TOTAL)"
echo "Results: $RUN_DIR"
echo "Summary: $SUMMARY_FILE"
echo "========================================"

# Return nonzero if any test failed
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi

exit 0
