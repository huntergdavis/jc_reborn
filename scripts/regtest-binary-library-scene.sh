#!/bin/bash
# regtest-binary-library-scene.sh — Run one scene across historical
# binary-library builds by repacking each build's executable with a
# scene-specific BOOTMODE.TXT and invoking the headless regtest harness.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# shellcheck source=./docker-common.sh
source "$PROJECT_ROOT/scripts/docker-common.sh"
docker_init

LIBRARY_DIR="$PROJECT_ROOT/binary-library"
INDEX_JSON="$LIBRARY_DIR/index.json"
REGTEST_SCENE_LIST="$PROJECT_ROOT/config/ps1/regtest-scenes.txt"
OUTPUT_ROOT=""
SCENE_SPEC=""
BOOT_STRING=""
REFERENCE_PATH=""
VLM_ENABLE=0
VLM_MODEL_DIR=""
VLM_BANK_DIR=""
VLM_SAMPLES="2"
COMPARE_MIN_RESULT_SCENE_FRAME=""
COMPARE_MIN_REFERENCE_SCENE_FRAME=""
COMPARE_ENTRY_MAX_DIFF=""
COMPARE_ENTRY_REFERENCE_WINDOW=""
COMPARE_SCENE_WINDOW_ONLY=0
SCENE_INDEX=""
SCENE_STATUS=""
START_SEQ=""
END_SEQ=""
LIMIT=""
RESUME=0
FRAMES="${REGTEST_FRAMES:-1800}"
START_FRAME=""
START_FRAME_EXPLICIT=0
MIN_TAIL_FRAMES="${REGTEST_SCENE_CAPTURE_MIN_TAIL_FRAMES:-1200}"
INTERVAL="${REGTEST_INTERVAL:-60}"
TIMEOUT="${REGTEST_TIMEOUT:-180}"
LOG_LEVEL="${REGTEST_LOG_LEVEL:-Warning}"
SEED="${REGTEST_SEED:-1}"
WORKTREE=""
WORKTREE_PARENT="$PROJECT_ROOT/.codex-tmp"

usage() {
    cat <<'USAGE'
Usage: regtest-binary-library-scene.sh --scene "ADS TAG" [options]

Options:
  --scene SPEC      Scene specification, e.g. "BUILDING 1"
  --boot STRING     Explicit BOOTMODE override instead of scene manifest lookup
  --reference PATH  Host/reference result dir or result.json for semantic compare
  --vlm             Run VLM scene-fix validation when --reference is present
  --vlm-model-dir PATH
                    Optional OpenVINO VLM model dir override
  --vlm-bank-dir PATH
                    Optional reference-bank dir for VLM hints
  --vlm-samples N   Number of VLM frame pairs per build (default: 2)
  --min-result-scene-frame N
                    Minimum result frame eligible as a scene-entry anchor
  --min-reference-scene-frame N
                    Minimum reference frame eligible as a scene-entry anchor
  --entry-max-diff N
                    Max palette-index diff allowed for scene-entry anchor match
  --entry-reference-window N
                    How many early reference frames to search for the anchor
  --scene-window-only
                    Only compare result frames that align into reference range
  --library DIR     Binary library root (default: ./binary-library)
  --output DIR      Output root (default: regtest-results/binlib-<scene>)
  --start-seq N     First binary-library sequence number to test
  --end-seq N       Last binary-library sequence number to test
  --limit N         Maximum number of matching builds to run
  --resume          Skip builds that already have result.json
  --frames N        Number of frames to run per build
  --start-frame N   First PS1 frame to keep in each build capture set
  --interval N      Screenshot interval
  --timeout N       Wall-clock timeout in seconds
  --log LEVEL       DuckStation log level
  --seed N          RNG seed appended to BOOTMODE if not already present
  -h, --help        Show this help
USAGE
}

while [ $# -gt 0 ]; do
    case "$1" in
        --scene) SCENE_SPEC="$2"; shift 2 ;;
        --boot) BOOT_STRING="$2"; shift 2 ;;
        --reference) REFERENCE_PATH="$2"; shift 2 ;;
        --vlm) VLM_ENABLE=1; shift ;;
        --vlm-model-dir) VLM_MODEL_DIR="$2"; shift 2 ;;
        --vlm-bank-dir) VLM_BANK_DIR="$2"; shift 2 ;;
        --vlm-samples) VLM_SAMPLES="$2"; shift 2 ;;
        --min-result-scene-frame) COMPARE_MIN_RESULT_SCENE_FRAME="$2"; shift 2 ;;
        --min-reference-scene-frame) COMPARE_MIN_REFERENCE_SCENE_FRAME="$2"; shift 2 ;;
        --entry-max-diff) COMPARE_ENTRY_MAX_DIFF="$2"; shift 2 ;;
        --entry-reference-window) COMPARE_ENTRY_REFERENCE_WINDOW="$2"; shift 2 ;;
        --scene-window-only) COMPARE_SCENE_WINDOW_ONLY=1; shift ;;
        --library) LIBRARY_DIR="$2"; shift 2 ;;
        --output) OUTPUT_ROOT="$2"; shift 2 ;;
        --start-seq) START_SEQ="$2"; shift 2 ;;
        --end-seq) END_SEQ="$2"; shift 2 ;;
        --limit) LIMIT="$2"; shift 2 ;;
        --resume) RESUME=1; shift ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --start-frame) START_FRAME="$2"; START_FRAME_EXPLICIT=1; shift 2 ;;
        --interval) INTERVAL="$2"; shift 2 ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --log) LOG_LEVEL="$2"; shift 2 ;;
        --seed) SEED="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if [ -z "$SCENE_SPEC" ]; then
    echo "ERROR: --scene is required" >&2
    exit 1
fi

if [ -z "$START_FRAME" ]; then
    grace="${REGTEST_BOOT_GRACE_FRAMES:-1800}"
    tolerance="${REGTEST_BOOT_GRACE_TOLERANCE_FRAMES:-120}"
    START_FRAME=$((grace - tolerance))
    if [ "$START_FRAME" -lt 0 ]; then
        START_FRAME=0
    fi
fi

if [ "$START_FRAME_EXPLICIT" -eq 0 ]; then
    min_frames=$((START_FRAME + MIN_TAIL_FRAMES))
    if [ "$FRAMES" -lt "$min_frames" ]; then
        FRAMES="$min_frames"
    fi
fi

if ! [[ "$START_FRAME" =~ ^[0-9]+$ ]]; then
    echo "ERROR: --start-frame must be an integer >= 0" >&2
    exit 1
fi
if [ "$START_FRAME" -lt 0 ]; then
    echo "ERROR: --start-frame must be >= 0" >&2
    exit 1
fi
if [ "$FRAMES" -lt "$START_FRAME" ]; then
    echo "ERROR: --frames must be >= --start-frame" >&2
    exit 1
fi

INDEX_JSON="$LIBRARY_DIR/index.json"
if [ ! -f "$INDEX_JSON" ]; then
    echo "ERROR: Missing binary library index: $INDEX_JSON" >&2
    exit 1
fi

read -r ADS_NAME SCENE_TAG <<< "$SCENE_SPEC"
if [ -z "${ADS_NAME:-}" ] || [ -z "${SCENE_TAG:-}" ]; then
    echo "ERROR: Scene spec must be 'ADS_NAME TAG', got '$SCENE_SPEC'" >&2
    exit 1
fi

lookup_scene_manifest() {
    local ads_name="$1"
    local scene_tag="$2"
    local line
    [ -f "$REGTEST_SCENE_LIST" ] || return 1
    line="$(awk -v ads="$ads_name" -v tag="$scene_tag" '
        $0 !~ /^[[:space:]]*#/ && NF >= 5 && $1 == ads && $2 == tag {
            print;
            exit;
        }
    ' "$REGTEST_SCENE_LIST")"
    [ -n "$line" ] || return 1
    printf '%s\n' "$line"
}

if [ -z "$BOOT_STRING" ]; then
    if SCENE_MANIFEST_LINE="$(lookup_scene_manifest "$ADS_NAME" "$SCENE_TAG")"; then
        SCENE_INDEX="$(printf '%s\n' "$SCENE_MANIFEST_LINE" | awk '{print $3}')"
        SCENE_STATUS="$(printf '%s\n' "$SCENE_MANIFEST_LINE" | awk '{print $4}')"
        BOOT_STRING="$(printf '%s\n' "$SCENE_MANIFEST_LINE" | cut -d' ' -f5-)"
    else
        BOOT_STRING="island ads ${ADS_NAME}.ADS ${SCENE_TAG}"
    fi
fi

if [[ "$BOOT_STRING" != *" seed "* ]] && [[ "$BOOT_STRING" != seed\ * ]] && [[ "$BOOT_STRING" != *" seed" ]]; then
    BOOT_STRING="${BOOT_STRING} seed ${SEED}"
fi

scene_slug="$(printf '%s-%s' "$ADS_NAME" "$SCENE_TAG" | tr '[:upper:]' '[:lower:]' | tr ' ' '-')"
if [ -z "$OUTPUT_ROOT" ]; then
    OUTPUT_ROOT="$PROJECT_ROOT/regtest-results/binlib-${scene_slug}"
fi
mkdir -p "$OUTPUT_ROOT"

JC_RESOURCES=""
for candidate in "$PROJECT_ROOT/jc_resources" "$HOME/jc_resources"; do
    if [ -f "$candidate/RESOURCE.MAP" ]; then
        JC_RESOURCES="$(realpath "$candidate")"
        break
    fi
done
if [ -z "$JC_RESOURCES" ]; then
    echo "ERROR: jc_resources/RESOURCE.MAP not found." >&2
    exit 1
fi

cleanup() {
    if [ -n "$WORKTREE" ] && [ -d "$WORKTREE" ]; then
        "${DOCKER_CMD[@]}" run --rm --platform linux/amd64 \
            -v "$WORKTREE":/project \
            jc-reborn-ps1-dev:amd64 \
            bash -c "chown -R $(id -u):$(id -g) /project 2>/dev/null || true" \
            >/dev/null 2>&1 || true
        git -C "$PROJECT_ROOT" worktree remove --force "$WORKTREE" 2>/dev/null || rm -rf "$WORKTREE"
    fi
}
trap cleanup EXIT

mkdir -p "$WORKTREE_PARENT"
WORKTREE="$WORKTREE_PARENT/jc_reborn_binlib_scene_$$"
git worktree add --detach --no-checkout "$WORKTREE" HEAD >/dev/null
git -C "$WORKTREE" sparse-checkout init --no-cone >/dev/null
git -C "$WORKTREE" sparse-checkout set \
    '/*.xml' \
    '/config/ps1/' \
    '/scripts/' \
    '/CMake*' \
    '/Makefile*' >/dev/null
git -C "$WORKTREE" checkout >/dev/null

run_dir_for_output() {
    local out="$1"
    find "$out" -mindepth 1 -maxdepth 1 -type d -exec test -f '{}/regtest.log' ';' -print | sort | tail -1
}

python3 - "$INDEX_JSON" "$LIBRARY_DIR" "$START_SEQ" "$END_SEQ" "$LIMIT" > "$OUTPUT_ROOT/.selected-builds.jsonl" <<'PY'
import json, sys
from pathlib import Path

index_path = Path(sys.argv[1])
library_dir = Path(sys.argv[2])
start_seq = int(sys.argv[3]) if sys.argv[3] else None
end_seq = int(sys.argv[4]) if sys.argv[4] else None
limit = int(sys.argv[5]) if sys.argv[5] else None

entries = json.loads(index_path.read_text())
count = 0
for entry in entries:
    seq = entry.get("sequence")
    build = entry.get("build", {})
    dir_name = entry.get("dir_name", "")
    cue_path = library_dir / dir_name / "jcreborn.cue"
    exe_path = library_dir / dir_name / "jcreborn.exe"
    if not build.get("iso_exists"):
        continue
    if start_seq is not None and seq < start_seq:
        continue
    if end_seq is not None and seq > end_seq:
        continue
    if not cue_path.is_file() or not exe_path.is_file():
        continue
    print(json.dumps(entry))
    count += 1
    if limit is not None and count >= limit:
        break
PY

TOTAL_SELECTED="$(wc -l < "$OUTPUT_ROOT/.selected-builds.jsonl" | tr -d ' ')"
if [ "$TOTAL_SELECTED" = "0" ]; then
    echo "No matching runnable builds found." >&2
    exit 1
fi

echo "Scene: $SCENE_SPEC"
echo "BOOTMODE: $BOOT_STRING"
if [ -n "$REFERENCE_PATH" ]; then
    echo "Reference: $REFERENCE_PATH"
fi
if [ "$VLM_ENABLE" -eq 1 ]; then
    echo "VLM: enabled"
fi
echo "Selected builds: $TOTAL_SELECTED"
echo "Output: $OUTPUT_ROOT"

SEQ_NO=0
while IFS= read -r entry_json; do
    SEQ_NO=$((SEQ_NO + 1))

    sequence="$(python3 - <<'PY' "$entry_json"
import json, sys
e = json.loads(sys.argv[1])
print(e["sequence"])
PY
)"
    dir_name="$(python3 - <<'PY' "$entry_json"
import json, sys
e = json.loads(sys.argv[1])
print(e["dir_name"])
PY
)"
    commit_hash="$(python3 - <<'PY' "$entry_json"
import json, sys
e = json.loads(sys.argv[1])
print(e["commit"]["hash"])
PY
)"
    short_hash="$(python3 - <<'PY' "$entry_json"
import json, sys
e = json.loads(sys.argv[1])
print(e["commit"]["short_hash"])
PY
)"
    build_dir="$LIBRARY_DIR/$dir_name"
    out_dir="$OUTPUT_ROOT/$dir_name"
    result_json="$out_dir/result.json"

    if [ "$RESUME" -eq 1 ] && [ -f "$result_json" ]; then
        printf "[%3d/%s] %s resume\n" "$SEQ_NO" "$TOTAL_SELECTED" "$short_hash"
        continue
    fi

    mkdir -p "$out_dir"
    printf "[%3d/%s] %s repack+run ... " "$SEQ_NO" "$TOTAL_SELECTED" "$short_hash"

    git -C "$WORKTREE" checkout --force "$commit_hash" >/dev/null 2>&1 || true
    git -C "$WORKTREE" clean -fdx >/dev/null 2>&1 || true

    mkdir -p "$WORKTREE/build-ps1" "$WORKTREE/config/ps1"
    printf '%s\n' "$BOOT_STRING" > "$WORKTREE/config/ps1/BOOTMODE.TXT"
    printf '%s\n' "$BOOT_STRING" > "$WORKTREE/BOOTMODE.TXT" 2>/dev/null || true
    cat > "$WORKTREE/config/ps1/bootmode_embedded.h" <<'HEADER'
#ifndef PS1_BOOTMODE_EMBEDDED_H
#define PS1_BOOTMODE_EMBEDDED_H
#define PS1_EMBEDDED_BOOT_OVERRIDE ""
#endif
HEADER

    cp "$build_dir/jcreborn.exe" "$WORKTREE/build-ps1/jcreborn.exe"
    for f in RESOURCE.MAP RESOURCE.001; do
        [ ! -f "$WORKTREE/$f" ] && [ -f "$JC_RESOURCES/$f" ] && cp "$JC_RESOURCES/$f" "$WORKTREE/$f" 2>/dev/null || true
    done
    [ ! -f "$WORKTREE/TITLE.RAW" ] && [ -f "$PROJECT_ROOT/TITLE.RAW" ] && cp "$PROJECT_ROOT/TITLE.RAW" "$WORKTREE/TITLE.RAW" 2>/dev/null || true
    rm -f "$WORKTREE/jc_resources" 2>/dev/null || true

    cd_layout=""
    if [ -f "$WORKTREE/config/ps1/cd_layout.xml" ]; then
        cd_layout="/project/config/ps1/cd_layout.xml"
    elif [ -f "$WORKTREE/cd_layout.xml" ]; then
        cd_layout="/project/cd_layout.xml"
    fi
    if [ -z "$cd_layout" ]; then
        echo "missing cd_layout"
        python3 - <<'PY' "$result_json" "$entry_json" "$SCENE_SPEC" "$BOOT_STRING"
import json, sys
entry = json.loads(sys.argv[2])
result = {
    "scene": {"spec": sys.argv[3], "boot_string": sys.argv[4]},
    "build": entry,
    "outcome": {"status": "missing_cd_layout", "exit_code": 1, "frames_captured": 0},
}
json.dump(result, open(sys.argv[1], "w"), indent=2)
print()
PY
        continue
    fi

    if ! "${DOCKER_CMD[@]}" run --rm --platform linux/amd64 \
        -v "$WORKTREE":/project \
        -v "$JC_RESOURCES":/project/jc_resources:ro \
        jc-reborn-ps1-dev:amd64 \
        bash -c "cd /project && mkpsxiso -y $cd_layout" \
        > "$out_dir/repack.log" 2>&1; then
        echo "repack failed"
        python3 - <<'PY' "$result_json" "$entry_json" "$SCENE_SPEC" "$BOOT_STRING" "$out_dir/repack.log"
import json, sys
entry = json.loads(sys.argv[2])
result = {
    "scene": {"spec": sys.argv[3], "boot_string": sys.argv[4]},
    "build": entry,
    "outcome": {"status": "repack_failed", "exit_code": 1, "frames_captured": 0},
    "paths": {"repack_log": sys.argv[5]},
}
json.dump(result, open(sys.argv[1], "w"), indent=2)
print()
PY
        continue
    fi

    REGTEST_EXIT=0
    "$PROJECT_ROOT/scripts/run-regtest.sh" \
        --cue "$WORKTREE/jcreborn.cue" \
        --frames "$FRAMES" \
        --dumpinterval "$INTERVAL" \
        --dumpdir "$out_dir" \
        --log "$LOG_LEVEL" \
        --timeout "$TIMEOUT" \
        > "$out_dir/driver.log" 2>&1 || REGTEST_EXIT=$?

    run_dir="$(run_dir_for_output "$out_dir")"
    frames_dir=""
    regtest_log="$out_dir/driver.log"
    printf_log=""
    frame_count=0
    state_hash=""
    timed_out=0
    has_fatal=0

    if [ -n "$run_dir" ]; then
        frames_dir="$run_dir/frames/jcreborn"
        if [ ! -d "$frames_dir" ]; then
            frames_dir="$run_dir/frames"
        fi
        if [ -f "$run_dir/regtest.log" ]; then
            regtest_log="$run_dir/regtest.log"
        fi
        if [ -f "$run_dir/tty-output.txt" ]; then
            printf_log="$run_dir/tty-output.txt"
        fi
    fi

    if [ -n "$frames_dir" ] && [ -d "$frames_dir" ]; then
        if [ "$START_FRAME" -gt 0 ]; then
            filtered_frames_dir="$out_dir/filtered-frames"
            rm -rf "$filtered_frames_dir"
            mkdir -p "$filtered_frames_dir"
            while IFS= read -r frame_path; do
                frame_name="$(basename "$frame_path")"
                frame_no="${frame_name#frame_}"
                frame_no="${frame_no%.png}"
                if [[ "$frame_no" =~ ^[0-9]+$ ]] && [ "$frame_no" -ge "$START_FRAME" ]; then
                    cp "$frame_path" "$filtered_frames_dir/$frame_name"
                fi
            done < <(find "$frames_dir" -type f -name 'frame_*.png' | sort)
            if find "$filtered_frames_dir" -maxdepth 1 -type f -name 'frame_*.png' | grep -q .; then
                frames_dir="$filtered_frames_dir"
            fi
        fi
        frame_count="$(find "$frames_dir" -type f -name 'frame_*.png' | wc -l | tr -d ' ')"
        last_frame="$(find "$frames_dir" -type f -name 'frame_*.png' | sort | tail -1)"
        if [ -n "$last_frame" ] && command -v sha256sum >/dev/null 2>&1; then
            state_hash="$(sha256sum "$last_frame" | cut -d' ' -f1)"
        fi
    fi

    if [ "$REGTEST_EXIT" -eq 124 ]; then
        timed_out=1
    fi
    if [ -n "$printf_log" ] && [ -f "$printf_log" ]; then
        if grep -qiE '(fatalError|FATAL|panic|assert|abort|crash)' "$printf_log" 2>/dev/null; then
            has_fatal=1
        fi
    fi

    python3 - <<'PY' "$result_json" "$entry_json" "$ADS_NAME" "$SCENE_TAG" "$SCENE_SPEC" "$SCENE_INDEX" "$SCENE_STATUS" "$BOOT_STRING" "$FRAMES" "$START_FRAME" "$INTERVAL" "$TIMEOUT" "$REGTEST_EXIT" "$timed_out" "$frame_count" "$state_hash" "$has_fatal" "$out_dir" "$frames_dir" "$regtest_log" "$printf_log" "$LIBRARY_DIR"
import json, os, sys

entry = json.loads(sys.argv[2])
scene_index = None if sys.argv[6] == "" else int(sys.argv[6])
library_dir = os.path.abspath(sys.argv[22])
result = {
    "scene": {
        "ads_name": sys.argv[3],
        "tag": int(sys.argv[4]),
        "spec": sys.argv[5],
        "scene_index": scene_index,
        "status": sys.argv[7] or None,
        "boot_string": sys.argv[8],
    },
    "config": {
        "frames": int(sys.argv[9]),
        "start_frame": int(sys.argv[10]),
        "interval": int(sys.argv[11]),
        "timeout": int(sys.argv[12]),
    },
    "build": {
        "sequence": entry["sequence"],
        "dir_name": entry["dir_name"],
        "commit": entry["commit"],
        "library_paths": {
            "dir": os.path.join(library_dir, entry["dir_name"]),
            "exe": os.path.join(library_dir, entry["dir_name"], "jcreborn.exe"),
            "cue": os.path.join(library_dir, entry["dir_name"], "jcreborn.cue"),
        },
    },
    "outcome": {
        "status": "pass" if int(sys.argv[13]) == 0 and int(sys.argv[15]) > 0 and int(sys.argv[17]) == 0 else "fail",
        "exit_code": int(sys.argv[13]),
        "timed_out": bool(int(sys.argv[14])),
        "frames_captured": int(sys.argv[15]),
        "state_hash": sys.argv[16] or None,
        "has_fatal_error": bool(int(sys.argv[17])),
    },
    "paths": {
        "output_dir": os.path.abspath(sys.argv[18]),
        "frames_dir": os.path.abspath(sys.argv[19]) if sys.argv[19] else None,
        "regtest_log": os.path.abspath(sys.argv[20]) if sys.argv[20] else None,
        "printf_log": os.path.abspath(sys.argv[21]) if sys.argv[21] else None,
        "repack_log": os.path.abspath(os.path.join(sys.argv[18], "repack.log")),
        "driver_log": os.path.abspath(os.path.join(sys.argv[18], "driver.log")),
    },
}
json.dump(result, open(sys.argv[1], "w"), indent=2)
print()
PY

    if [ -n "$REFERENCE_PATH" ]; then
        compare_json="$out_dir/compare.json"
        compare_args=(
            --json
            --scene-entry-align
            --result "$out_dir"
            --reference "$REFERENCE_PATH"
        )
        if [ -n "$COMPARE_MIN_RESULT_SCENE_FRAME" ]; then
            compare_args+=(--min-result-scene-frame "$COMPARE_MIN_RESULT_SCENE_FRAME")
        fi
        if [ -n "$COMPARE_MIN_REFERENCE_SCENE_FRAME" ]; then
            compare_args+=(--min-reference-scene-frame "$COMPARE_MIN_REFERENCE_SCENE_FRAME")
        fi
        if [ -n "$COMPARE_ENTRY_MAX_DIFF" ]; then
            compare_args+=(--entry-max-diff "$COMPARE_ENTRY_MAX_DIFF")
        fi
        if [ -n "$COMPARE_ENTRY_REFERENCE_WINDOW" ]; then
            compare_args+=(--entry-reference-window "$COMPARE_ENTRY_REFERENCE_WINDOW")
        fi
        if [ "$COMPARE_SCENE_WINDOW_ONLY" -eq 1 ]; then
            compare_args+=(--scene-window-only)
        fi
        if python3 "$PROJECT_ROOT/scripts/compare-sequence-runs.py" \
            "${compare_args[@]}" \
            > "$compare_json" 2> "$out_dir/compare.stderr"; then
            python3 - <<'PY' "$result_json" "$compare_json" "$COMPARE_MIN_RESULT_SCENE_FRAME" "$COMPARE_MIN_REFERENCE_SCENE_FRAME" "$COMPARE_ENTRY_MAX_DIFF" "$COMPARE_ENTRY_REFERENCE_WINDOW" "$COMPARE_SCENE_WINDOW_ONLY"
import json, sys

result_path, compare_path = sys.argv[1], sys.argv[2]
result = json.load(open(result_path))
compare = json.load(open(compare_path))

semantic_status = "unknown"
if compare.get("error"):
    semantic_status = "no_anchor"
else:
    verdict = compare.get("verdict")
    if verdict == "MATCH":
        semantic_status = "match"
    elif verdict == "TIMING_MISMATCH":
        semantic_status = "timing_mismatch"
    elif verdict == "PIXEL_MISMATCH":
        semantic_status = "pixel_mismatch"
    elif verdict == "ALIGNMENT_FAILED":
        semantic_status = "alignment_failed"

result["compare"] = {
    "semantic_status": semantic_status,
    "verdict": compare.get("verdict"),
    "error": compare.get("error"),
    "alignment_mode": compare.get("alignment_mode"),
    "result_entry_frame": compare.get("result_entry_frame"),
    "reference_entry_frame": compare.get("reference_entry_frame"),
    "common_frame_count": compare.get("common_frame_count"),
    "result_scene_candidate_count": compare.get("result_scene_candidate_count"),
    "result_coverage_ratio": compare.get("result_coverage_ratio"),
    "reference_coverage_ratio": compare.get("reference_coverage_ratio"),
    "all_frames_match": compare.get("all_frames_match"),
    "min_result_scene_frame": int(sys.argv[3]) if sys.argv[3] else None,
    "min_reference_scene_frame": int(sys.argv[4]) if sys.argv[4] else None,
    "entry_max_diff": int(sys.argv[5]) if sys.argv[5] else None,
    "entry_reference_window": int(sys.argv[6]) if sys.argv[6] else None,
    "scene_window_only": bool(int(sys.argv[7])),
}
result.setdefault("paths", {})["compare_json"] = compare_path
json.dump(result, open(result_path, "w"), indent=2)
print()
PY
        else
            rm -f "$compare_json"
        fi

        if [ "$VLM_ENABLE" -eq 1 ]; then
            vlm_json="$out_dir/vlm-scene-fix.json"
            vlm_args=(
                scene-fix
                --reference "$REFERENCE_PATH"
                --result "$out_dir"
                --out-json "$vlm_json"
                --scene-id "${ADS_NAME}-${SCENE_TAG}"
                --samples "$VLM_SAMPLES"
            )
            if [ -n "$VLM_MODEL_DIR" ]; then
                vlm_args+=(--model-dir "$VLM_MODEL_DIR")
            fi
            if [ -n "$VLM_BANK_DIR" ]; then
                vlm_args+=(--bank-dir "$VLM_BANK_DIR")
            fi
            if python3 "$PROJECT_ROOT/scripts/validate-ps1-vlm.py" \
                "${vlm_args[@]}" \
                > "$out_dir/vlm.stdout" 2> "$out_dir/vlm.stderr"; then
                python3 - <<'PY' "$result_json" "$vlm_json"
import json, sys

result_path, vlm_path = sys.argv[1], sys.argv[2]
result = json.load(open(result_path))
vlm = json.load(open(vlm_path))
result.setdefault("compare", {})["vlm"] = {
    "verdict": vlm.get("verdict"),
    "pair_count": vlm.get("pair_count"),
    "label_counts": vlm.get("label_counts", {}),
    "dominant_hint_scene": vlm.get("dominant_hint_scene"),
}
result.setdefault("paths", {})["vlm_json"] = vlm_path
json.dump(result, open(result_path, "w"), indent=2)
print()
PY
            else
                rm -f "$vlm_json"
            fi
        fi
    fi

    if [ "$REGTEST_EXIT" -eq 0 ] && [ "$frame_count" -gt 0 ] && [ "$has_fatal" -eq 0 ]; then
        echo "pass (${frame_count} frames)"
    else
        echo "fail (${frame_count} frames, exit ${REGTEST_EXIT})"
    fi
done < "$OUTPUT_ROOT/.selected-builds.jsonl"

python3 - "$OUTPUT_ROOT" > "$OUTPUT_ROOT/index.json" <<'PY'
import json, sys
from pathlib import Path

root = Path(sys.argv[1])
entries = []
for path in sorted(root.glob("*/result.json")):
    entries.append(json.loads(path.read_text()))
json.dump(entries, sys.stdout, indent=2)
print()
PY

python3 - "$OUTPUT_ROOT/index.json" > "$OUTPUT_ROOT/index.csv" <<'PY'
import csv, json, sys

entries = json.load(open(sys.argv[1]))
w = csv.writer(sys.stdout)
w.writerow(["sequence", "short_hash", "date", "scene", "status", "semantic_status", "vlm_status", "state_hash", "exit_code", "frames_captured", "message"])
for entry in entries:
    build = entry.get("build", {})
    commit = build.get("commit", {})
    outcome = entry.get("outcome", {})
    scene = entry.get("scene", {})
    compare = entry.get("compare", {})
    vlm = compare.get("vlm", {})
    w.writerow([
        build.get("sequence", ""),
        commit.get("short_hash", ""),
        commit.get("date", ""),
        scene.get("spec", ""),
        outcome.get("status", ""),
        compare.get("semantic_status", ""),
        vlm.get("verdict", ""),
        outcome.get("state_hash", ""),
        outcome.get("exit_code", ""),
        outcome.get("frames_captured", ""),
        commit.get("message", ""),
    ])
PY

python3 - "$OUTPUT_ROOT/index.json" > "$OUTPUT_ROOT/SUMMARY.txt" <<'PY'
import json, sys

entries = json.load(open(sys.argv[1]))
passes = sum(1 for e in entries if e.get("outcome", {}).get("status") == "pass")
fails = len(entries) - passes
semantic = {}
vlm = {}
for entry in entries:
    key = entry.get("compare", {}).get("semantic_status")
    if not key:
        continue
    semantic[key] = semantic.get(key, 0) + 1
for entry in entries:
    key = entry.get("compare", {}).get("vlm", {}).get("verdict")
    if not key:
        continue
    vlm[key] = vlm.get(key, 0) + 1
scene = entries[0]["scene"]["spec"] if entries else "unknown"
print("Binary Library Scene Sweep")
print("=========================")
print(f"Scene:  {scene}")
print(f"Runs:   {len(entries)}")
print(f"Pass:   {passes}")
print(f"Fail:   {fails}")
if semantic:
    print("Semantic:")
    for key in sorted(semantic):
        print(f"  {key}: {semantic[key]}")
if vlm:
    print("VLM:")
    for key in sorted(vlm):
        print(f"  {key}: {vlm[key]}")
PY

rm -f "$OUTPUT_ROOT/.selected-builds.jsonl"
echo "Wrote:"
echo "  $OUTPUT_ROOT/index.json"
echo "  $OUTPUT_ROOT/index.csv"
echo "  $OUTPUT_ROOT/SUMMARY.txt"
