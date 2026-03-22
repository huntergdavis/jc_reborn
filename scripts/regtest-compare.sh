#!/bin/bash
# regtest-compare.sh — Compare two regtest runs (before/after a code change).
#
# Compares:
#   - State hashes for each scene
#   - Telemetry values (drop counts, memory, threads)
#   - Visual frame diffs (if ImageMagick is available)
#   - Summary of regressions and improvements
#
# Usage:
#   ./scripts/regtest-compare.sh results/run-before/ results/run-after/
#   ./scripts/regtest-compare.sh --scene "STAND 2" dir-a/ dir-b/
#   ./scripts/regtest-compare.sh --json dir-a/ dir-b/

set -euo pipefail

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
JSON_OUTPUT=0
SCENE_FILTER=""
DIFF_DIR=""

usage() {
    cat <<'USAGE'
Usage: regtest-compare.sh [OPTIONS] <dir-before> <dir-after>

Compare two regtest result directories and report differences.

Options:
  --scene SPEC     Only compare this scene (e.g. "STAND 2")
  --json           Output JSON instead of human-readable table
  --diff-dir DIR   Directory to write visual diff images
  -h, --help       Show this help

Arguments:
  dir-before       First (baseline) regtest results directory
  dir-after        Second (current) regtest results directory

Either directory can be:
  - A run directory (e.g. regtest-results/run-20260321-120000/)
    containing per-scene subdirectories
  - A single scene directory (e.g. regtest-results/stand-2/)
    containing result.json and frames/
USAGE
    exit 0
}

POSITIONAL=()

while [ $# -gt 0 ]; do
    case "$1" in
        --scene)      SCENE_FILTER="$2"; shift 2 ;;
        --json)       JSON_OUTPUT=1; shift ;;
        --diff-dir)   DIFF_DIR="$2"; shift 2 ;;
        -h|--help)    usage ;;
        *)            POSITIONAL+=("$1"); shift ;;
    esac
done

if [ ${#POSITIONAL[@]} -ne 2 ]; then
    echo "ERROR: Expected exactly 2 directory arguments." >&2
    echo "Run with --help for usage." >&2
    exit 1
fi

DIR_A="${POSITIONAL[0]}"
DIR_B="${POSITIONAL[1]}"

if [ ! -d "$DIR_A" ]; then
    echo "ERROR: Directory not found: $DIR_A" >&2
    exit 1
fi
if [ ! -d "$DIR_B" ]; then
    echo "ERROR: Directory not found: $DIR_B" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Discover scene directories
# ---------------------------------------------------------------------------
find_scene_dirs() {
    local base="$1"
    local -n out_dirs="$2"
    local -n out_labels="$3"

    # If this directory itself has a result.json, it's a single scene dir
    if [ -f "$base/result.json" ]; then
        out_dirs+=("$base")
        local ads_name tag
        ads_name="$(python3 -c "import json; print(json.load(open('$base/result.json'))['scene']['ads_name'])" 2>/dev/null || echo "unknown")"
        tag="$(python3 -c "import json; print(json.load(open('$base/result.json'))['scene']['tag'])" 2>/dev/null || echo "0")"
        out_labels+=("${ads_name,,}-${tag}")
        return
    fi

    # Otherwise enumerate subdirectories with result.json
    for sub in "$base"/*/; do
        [ -f "$sub/result.json" ] || continue
        local label
        label="$(basename "$sub")"
        if [ -n "$SCENE_FILTER" ]; then
            local ads_name tag
            read -r ads_name tag <<< "$SCENE_FILTER"
            local expected="${ads_name,,}-${tag}"
            [ "$label" = "$expected" ] || continue
        fi
        out_dirs+=("$sub")
        out_labels+=("$label")
    done
}

declare -a DIRS_A=() LABELS_A=()
declare -a DIRS_B=() LABELS_B=()

find_scene_dirs "$DIR_A" DIRS_A LABELS_A
find_scene_dirs "$DIR_B" DIRS_B LABELS_B

# Build lookup for dir B by label
declare -A B_BY_LABEL=()
for i in "${!LABELS_B[@]}"; do
    B_BY_LABEL["${LABELS_B[$i]}"]="${DIRS_B[$i]}"
done

if [ ${#DIRS_A[@]} -eq 0 ]; then
    echo "ERROR: No scene results found in $DIR_A" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Comparison helpers
# ---------------------------------------------------------------------------
extract_hash() {
    local dir="$1"
    python3 -c "
import json, os
r = json.load(open(os.path.join('$dir', 'result.json')))
h = r.get('outcome', {}).get('state_hash', '')
print(h if h else 'none')
" 2>/dev/null || echo "none"
}

extract_drops() {
    local dir="$1"
    python3 -c "
import json, os
r = json.load(open(os.path.join('$dir', 'result.json')))
d = r.get('outcome', {}).get('drop_indicators', {})
thread = d.get('drop_thread_drops', 0) or d.get('thread_drops', 0) or 0
bmp = d.get('drop_bmp_frame_cap', 0) or d.get('bmp_frame_cap', 0) or 0
print(f'{thread} {bmp}')
" 2>/dev/null || echo "0 0"
}

extract_frames() {
    local dir="$1"
    python3 -c "
import json, os
r = json.load(open(os.path.join('$dir', 'result.json')))
print(r.get('outcome', {}).get('frames_captured', 0))
" 2>/dev/null || echo "0"
}

extract_fatal() {
    local dir="$1"
    python3 -c "
import json, os
r = json.load(open(os.path.join('$dir', 'result.json')))
print('yes' if r.get('outcome', {}).get('has_fatal_error', False) else 'no')
" 2>/dev/null || echo "?"
}

# Visual diff using ImageMagick compare
make_visual_diff() {
    local img_a="$1"
    local img_b="$2"
    local img_out="$3"

    if ! command -v compare >/dev/null 2>&1; then
        return 1
    fi

    compare -metric AE "$img_a" "$img_b" "$img_out" 2>/dev/null || true
    return 0
}

# ---------------------------------------------------------------------------
# Compare all matched scenes
# ---------------------------------------------------------------------------
declare -a COMP_RESULTS=()
REGRESSIONS=0
IMPROVEMENTS=0
UNCHANGED=0
MISSING_B=0

if [ "$JSON_OUTPUT" -eq 0 ]; then
    echo "========================================"
    echo "Regtest Comparison"
    echo "  Before: $DIR_A"
    echo "  After:  $DIR_B"
    echo "========================================"
    echo ""
    printf "%-20s %8s %8s %8s %8s %10s\n" "SCENE" "FRAMES" "HASH" "DROPS" "FATAL" "VERDICT"
    printf "%-20s %8s %8s %8s %8s %10s\n" "--------------------" "--------" "--------" "--------" "--------" "----------"
fi

JSON_SCENES="["
JSON_FIRST=1

for i in "${!LABELS_A[@]}"; do
    label="${LABELS_A[$i]}"
    dir_a="${DIRS_A[$i]}"
    dir_b="${B_BY_LABEL[$label]:-}"

    if [ -z "$dir_b" ]; then
        MISSING_B=$((MISSING_B + 1))
        if [ "$JSON_OUTPUT" -eq 0 ]; then
            printf "%-20s %8s %8s %8s %8s %10s\n" "$label" "-" "-" "-" "-" "MISSING_B"
        fi
        continue
    fi

    # Extract values
    hash_a="$(extract_hash "$dir_a")"
    hash_b="$(extract_hash "$dir_b")"
    read -r drops_a_thread drops_a_bmp <<< "$(extract_drops "$dir_a")"
    read -r drops_b_thread drops_b_bmp <<< "$(extract_drops "$dir_b")"
    frames_a="$(extract_frames "$dir_a")"
    frames_b="$(extract_frames "$dir_b")"
    fatal_a="$(extract_fatal "$dir_a")"
    fatal_b="$(extract_fatal "$dir_b")"

    # Determine verdict
    verdict="same"
    hash_status="same"
    drops_status="same"
    fatal_status="same"
    frames_status="same"

    if [ "$hash_a" != "$hash_b" ]; then
        hash_status="changed"
    fi

    if [ "$drops_b_thread" -gt "$drops_a_thread" ] 2>/dev/null; then
        drops_status="worse"
    elif [ "$drops_b_thread" -lt "$drops_a_thread" ] 2>/dev/null; then
        drops_status="better"
    fi

    if [ "$fatal_a" = "no" ] && [ "$fatal_b" = "yes" ]; then
        fatal_status="regression"
    elif [ "$fatal_a" = "yes" ] && [ "$fatal_b" = "no" ]; then
        fatal_status="fixed"
    fi

    if [ "$frames_a" -gt 0 ] && [ "$frames_b" -eq 0 ] 2>/dev/null; then
        frames_status="regression"
    elif [ "$frames_a" -eq 0 ] && [ "$frames_b" -gt 0 ] 2>/dev/null; then
        frames_status="fixed"
    fi

    # Overall verdict
    if [ "$fatal_status" = "regression" ] || [ "$frames_status" = "regression" ] || [ "$drops_status" = "worse" ]; then
        verdict="REGRESS"
        REGRESSIONS=$((REGRESSIONS + 1))
    elif [ "$fatal_status" = "fixed" ] || [ "$frames_status" = "fixed" ] || [ "$drops_status" = "better" ]; then
        verdict="IMPROVED"
        IMPROVEMENTS=$((IMPROVEMENTS + 1))
    elif [ "$hash_status" = "changed" ]; then
        verdict="CHANGED"
        UNCHANGED=$((UNCHANGED + 1))
    else
        verdict="same"
        UNCHANGED=$((UNCHANGED + 1))
    fi

    if [ "$JSON_OUTPUT" -eq 0 ]; then
        hash_disp="same"
        [ "$hash_status" = "changed" ] && hash_disp="differ"
        drops_disp="${drops_a_thread}->${drops_b_thread}"
        fatal_disp="$fatal_a->$fatal_b"
        frames_disp="${frames_a}->${frames_b}"

        printf "%-20s %8s %8s %8s %8s %10s\n" \
            "$label" "$frames_disp" "$hash_disp" "$drops_disp" "$fatal_disp" "$verdict"
    fi

    # Visual diff if requested
    if [ -n "$DIFF_DIR" ] && [ "$hash_status" = "changed" ]; then
        mkdir -p "$DIFF_DIR"
        # Diff last frame of each
        last_a="$(find "$dir_a/frames" -name '*.png' 2>/dev/null | sort | tail -1)"
        last_b="$(find "$dir_b/frames" -name '*.png' 2>/dev/null | sort | tail -1)"
        if [ -n "$last_a" ] && [ -n "$last_b" ]; then
            diff_out="$DIFF_DIR/${label}-diff.png"
            make_visual_diff "$last_a" "$last_b" "$diff_out" && true
        fi
    fi

    # JSON entry
    if [ "$JSON_FIRST" -eq 0 ]; then JSON_SCENES+=","; fi
    JSON_FIRST=0
    JSON_SCENES+="$(python3 -c "
import json
entry = {
    'scene': '$label',
    'verdict': '$verdict',
    'hash': {'before': '$hash_a'[:16], 'after': '$hash_b'[:16], 'status': '$hash_status'},
    'drops': {'before': $drops_a_thread, 'after': $drops_b_thread, 'status': '$drops_status'},
    'fatal': {'before': '$fatal_a' == 'yes', 'after': '$fatal_b' == 'yes', 'status': '$fatal_status'},
    'frames': {'before': $frames_a, 'after': $frames_b, 'status': '$frames_status'},
}
print(json.dumps(entry))
")"
done

JSON_SCENES+="]"

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
TOTAL=$((REGRESSIONS + IMPROVEMENTS + UNCHANGED + MISSING_B))

if [ "$JSON_OUTPUT" -eq 1 ]; then
    python3 -c "
import json, sys
scenes = json.loads(sys.stdin.read())
summary = {
    'before': '$DIR_A',
    'after': '$DIR_B',
    'total': $TOTAL,
    'regressions': $REGRESSIONS,
    'improvements': $IMPROVEMENTS,
    'unchanged': $UNCHANGED,
    'missing_in_after': $MISSING_B,
    'scenes': scenes,
}
print(json.dumps(summary, indent=2))
" <<< "$JSON_SCENES"
else
    echo ""
    echo "========================================"
    echo "Regressions: $REGRESSIONS"
    echo "Improvements: $IMPROVEMENTS"
    echo "Unchanged/Changed: $UNCHANGED"
    if [ "$MISSING_B" -gt 0 ]; then
        echo "Missing in after: $MISSING_B"
    fi
    echo "Total: $TOTAL"
    echo "========================================"
fi

# Return nonzero if regressions found
if [ "$REGRESSIONS" -gt 0 ]; then
    exit 1
fi

exit 0
