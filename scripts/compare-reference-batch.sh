#!/bin/bash
# compare-reference-batch.sh — Run live scene captures and compare them against
# a reference scene set using masked BEST/ENTRY/LASTSCN hashes.

set -euo pipefail

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

REFERENCE_DIR="$PROJECT_ROOT/regtest-references"
OUTPUT_ROOT="$PROJECT_ROOT/regtest-results/reference-compare"
FRAMES=4200
START_FRAME=0
INTERVAL=1
SEED=""
SKIP_BUILD=0
SUMMARY_FILE=""
SCENE_FILE=""
STATUS_FILTER=""
LIMIT=""
MIN_RESULT_SCENE_FRAME=""
MIN_REFERENCE_SCENE_FRAME="0"
SCENE_WINDOW_ONLY=1
COMPARE_TIMEOUT="${COMPARE_TIMEOUT:-90}"
declare -a SCENES=()

usage() {
    cat <<'USAGE'
Usage: compare-reference-batch.sh [OPTIONS] [--scene "ADS TAG"...]

Options:
  --scene SPEC       Scene spec, e.g. "BUILDING 1" (repeatable)
  --scene-file F     Read scenes from regtest-style scene list
  --status NAME      Filter scene-file rows by status (e.g. verified)
  --limit N          Limit number of scenes loaded from scene-file
  --reference DIR    Reference root (default: regtest-references/)
  --output DIR       Output root for fresh runs (default: regtest-results/reference-compare/)
  --seed N           Force deterministic BOOTMODE RNG seed
  --frames N         Frames per run (default: 4200)
  --start-frame N    First PS1 frame to keep in the capture set (default: 0)
  --interval N       Capture interval (default: 1)
  --min-result-scene-frame N
                     Minimum PS1 frame eligible to align as scene entry
                     (default: boot grace minus tolerance)
  --min-reference-scene-frame N
                     Minimum reference frame eligible to align as scene entry
                     (default: 0)
  --no-scene-window-only
                     Compare against the full PS1 capture instead of clipping to the host scene window
  --summary FILE     Optional JSON summary output path
  --skip-build       Pass through to regtest-scene.sh
  -h, --help         Show this help
USAGE
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --scene)      SCENES+=("$2"); shift 2 ;;
        --scene-file) SCENE_FILE="$2"; shift 2 ;;
        --status)     STATUS_FILTER="$2"; shift 2 ;;
        --limit)      LIMIT="$2"; shift 2 ;;
        --reference)  REFERENCE_DIR="$2"; shift 2 ;;
        --output)     OUTPUT_ROOT="$2"; shift 2 ;;
        --seed)       SEED="$2"; shift 2 ;;
        --frames)     FRAMES="$2"; shift 2 ;;
        --start-frame) START_FRAME="$2"; shift 2 ;;
        --interval)   INTERVAL="$2"; shift 2 ;;
        --min-result-scene-frame) MIN_RESULT_SCENE_FRAME="$2"; shift 2 ;;
        --min-reference-scene-frame) MIN_REFERENCE_SCENE_FRAME="$2"; shift 2 ;;
        --no-scene-window-only) SCENE_WINDOW_ONLY=0; shift ;;
        --summary)    SUMMARY_FILE="$2"; shift 2 ;;
        --skip-build) SKIP_BUILD=1; shift ;;
        -h|--help)    usage ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if [ -z "$MIN_RESULT_SCENE_FRAME" ]; then
    grace="${REGTEST_BOOT_GRACE_FRAMES:-1800}"
    tolerance="${REGTEST_BOOT_GRACE_TOLERANCE_FRAMES:-120}"
    MIN_RESULT_SCENE_FRAME=$((grace - tolerance))
    if [ "$MIN_RESULT_SCENE_FRAME" -lt 0 ]; then
        MIN_RESULT_SCENE_FRAME=0
    fi
fi

if [ -n "$SCENE_FILE" ]; then
    mapfile -t FILE_SCENES < <(python3 - "$SCENE_FILE" "$STATUS_FILTER" "$LIMIT" <<'PY'
import sys
from pathlib import Path

scene_file = Path(sys.argv[1])
status_filter = sys.argv[2]
limit_raw = sys.argv[3]
limit = int(limit_raw) if limit_raw else None

count = 0
for raw in scene_file.read_text(encoding="utf-8").splitlines():
    line = raw.split("#", 1)[0].strip()
    if not line:
        continue
    parts = line.split()
    if len(parts) < 4:
        continue
    ads_name, tag, _scene_index, status = parts[:4]
    if status_filter and status != status_filter:
        continue
    print(f"{ads_name} {tag}")
    count += 1
    if limit is not None and count >= limit:
        break
PY
)
    SCENES+=("${FILE_SCENES[@]}")
fi

if [ ${#SCENES[@]} -eq 0 ]; then
    echo "ERROR: At least one scene must come from --scene or --scene-file." >&2
    usage
fi

lookup_scene_defaults() {
    python3 - "$PROJECT_ROOT/config/ps1/regtest-scenes.txt" "$1" "$2" <<'PY'
import sys
from pathlib import Path

scene_file = Path(sys.argv[1])
ads_name = sys.argv[2]
scene_tag = sys.argv[3]

for raw in scene_file.read_text(encoding="utf-8").splitlines():
    line = raw.split("#", 1)[0].strip()
    if not line:
        continue
    parts = line.split()
    if len(parts) < 6:
        continue
    if parts[0] == ads_name and parts[1] == scene_tag:
        print(f"{parts[2]}\t{parts[3]}\t{' '.join(parts[4:])}")
        raise SystemExit(0)
raise SystemExit(1)
PY
}

declare -a SUMMARY_ROWS=()

for scene in "${SCENES[@]}"; do
    read -r ADS_NAME TAG <<< "$scene"
    LABEL_BASE="${ADS_NAME,,}-${TAG}"
    RESULT_DIR="$OUTPUT_ROOT/${LABEL_BASE}"
    REFERENCE_META="$REFERENCE_DIR/${ADS_NAME}-${TAG}/metadata.json"
    mkdir -p "$OUTPUT_ROOT"
    mkdir -p "$RESULT_DIR"

    if [ ! -f "$REFERENCE_META" ]; then
        echo "ERROR: Reference metadata not found for $scene at $REFERENCE_META" >&2
        exit 1
    fi

    SCENE_INDEX=""
    STATUS=""
    DEFAULT_BOOT=""
    if SCENE_DEFAULTS="$(lookup_scene_defaults "$ADS_NAME" "$TAG")"; then
        IFS=$'\t' read -r SCENE_INDEX STATUS DEFAULT_BOOT <<< "$SCENE_DEFAULTS"
    else
        echo "ERROR: Could not derive scene defaults for $scene" >&2
        exit 1
    fi

    BOOT_STRING="$DEFAULT_BOOT"
    if [ -n "$SEED" ]; then
        BOOT_STRING="$BOOT_STRING seed $SEED"
    fi

    echo "== run $scene =="
    CMD=(
        "$SCRIPT_DIR/regtest-scene.sh"
        --scene "$scene"
        --scene-index "$SCENE_INDEX"
        --status "$STATUS"
        --boot "$BOOT_STRING"
        --frames "$FRAMES"
        --start-frame "$START_FRAME"
        --interval "$INTERVAL"
        --output "$RESULT_DIR"
    )
    if [ "$SKIP_BUILD" -eq 1 ]; then
        CMD+=(--skip-build)
    fi

    set +e
    "${CMD[@]}" > "$RESULT_DIR.result.json"
    rc=$?
    set -e
    if [ "$rc" -ne 0 ]; then
        echo "[compare-ref] $scene exited with code $rc; preserving result for comparison." >&2
    fi

    if [ ! -s "$RESULT_DIR.result.json" ]; then
        echo "[compare-ref] $scene produced no result JSON; skipping compare." >&2
        continue
    fi

    COMPARE_CMD=(
        python3 "$SCRIPT_DIR/compare-sequence-runs.py"
        --json \
        --scene-entry-align \
        --result "$RESULT_DIR.result.json" \
        --reference "$REFERENCE_DIR/${ADS_NAME}-${TAG}" \
        --min-result-scene-frame "$MIN_RESULT_SCENE_FRAME" \
        --min-reference-scene-frame "$MIN_REFERENCE_SCENE_FRAME"
    )
    if [ "$SCENE_WINDOW_ONLY" -eq 1 ]; then
        COMPARE_CMD+=(--scene-window-only)
    fi
    COMPARE_JSON_PATH="$RESULT_DIR/compare.json"
    COMPARE_HTML_PATH="$RESULT_DIR/compare.html"
    set +e
    COMPARE_JSON="$(timeout "${COMPARE_TIMEOUT}s" "${COMPARE_CMD[@]}")"
    compare_rc=$?
    set -e
    if [ "$compare_rc" -ne 0 ]; then
        COMPARE_JSON="$(python3 - <<'PY' "$RESULT_DIR.result.json" "$REFERENCE_DIR/${ADS_NAME}-${TAG}/result.json" "$MIN_RESULT_SCENE_FRAME" "$MIN_REFERENCE_SCENE_FRAME" "$scene" "$compare_rc"
import json
import sys

result_path = sys.argv[1]
reference_path = sys.argv[2]
min_result = int(sys.argv[3])
min_reference = int(sys.argv[4])
scene_label = sys.argv[5]
rc = int(sys.argv[6])

error = "compare timeout" if rc == 124 else f"compare failed rc={rc}"
payload = {
    "scene": scene_label,
    "result": result_path,
    "reference": reference_path,
    "alignment_mode": "scene-entry-align",
    "frame_offset": None,
    "result_entry_frame": None,
    "reference_entry_frame": None,
    "min_result_scene_frame": min_result,
    "min_reference_scene_frame": min_reference,
    "common_frame_count": 0,
    "average_palette_index_diff_pixels": None,
    "result_only_frames": [],
    "reference_only_frames": [],
    "frames": [],
    "error": error,
    "verdict": "COMPARE_FAILED",
}
print(json.dumps(payload))
PY
)"
        echo "[compare-ref] compare fallback for $scene ($compare_rc)"
    fi
    printf '%s\n' "$COMPARE_JSON" > "$COMPARE_JSON_PATH"
    python3 "$SCRIPT_DIR/render-compare-timeline.py" \
        --compare-json "$COMPARE_JSON_PATH" \
        --output "$COMPARE_HTML_PATH" \
        --title "$scene PS1 vs Reference" \
        >/dev/null
    echo "[compare-ref] html: $COMPARE_HTML_PATH"
    SUMMARY_ROWS+=("$COMPARE_JSON")
    echo
done

python3 - <<'PY' "$SUMMARY_FILE" "${SUMMARY_ROWS[@]}"
import json, sys

summary_path = sys.argv[1]
rows = [json.loads(raw) for raw in sys.argv[2:]]
payload = {
    "scenes": rows,
    "counts": {},
}
counts = {}
for row in rows:
    verdict = row.get("verdict", "UNKNOWN")
    counts[verdict] = counts.get(verdict, 0) + 1
payload["counts"] = counts

print(json.dumps(payload, indent=2))
if summary_path:
    with open(summary_path, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2)
        f.write("\n")
PY
