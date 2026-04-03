#!/bin/bash
# capture-and-check-ps1.sh — Run an overlay-backed PS1 capture through the
# headless regtest harness by default, compare the resulting screenshot against
# expected character truth, and open the HTML diff.

set -euo pipefail

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

if [ -f "$PROJECT_ROOT/config/ps1/regtest-config.sh" ]; then
    # shellcheck source=../config/ps1/regtest-config.sh
    source "$PROJECT_ROOT/config/ps1/regtest-config.sh"
fi

MODE="headless"
WAIT_TIME=35
FRAMES="${REGTEST_FRAMES:-9000}"
INTERVAL="${REGTEST_INTERVAL:-60}"
USE_BASELINE_MASK=1
EXPECTED_ROOT=""
LOOKUP_ROOT=""
OUT_DIR="/tmp/ps1-bug-check"
SCENE_LABEL=""
FRAME_NUMBER=""
ACTUAL_FRAME=""
OPEN_REPORT=1
IMAGE_PATH=""
SCENE_SPEC=""
BOOT_ARGS=()

usage() {
    cat <<'USAGE'
Usage: capture-and-check-ps1.sh --expected-root PATH [OPTIONS] [BOOTMODE...]

Options:
  --expected-root PATH  Expected scene/corpus root used to build truth
  --lookup-root PATH    Root used to resolve overlay sprite hashes (default: expected root)
  --image PATH          Reuse an existing overlay screenshot instead of launching DuckStation
  --scene SPEC          Scene spec for headless regtest capture, e.g. "FISHING 1"
  --frames N            Headless regtest frame budget (default: REGTEST_FRAMES or 9000)
  --interval N          Headless regtest dump interval (default: REGTEST_INTERVAL or 60)
  --actual-frame N      Use frame_NNNNN.png from the headless run instead of the last dumped frame
  --no-baseline-mask    Skip the paired headless overlay-mask baseline capture
  --live                Use the older live DuckStation path instead of headless regtest
  --wait N              Seconds before first screenshot (default: 35)
  --out-dir PATH        Output directory for diff artifacts (default: /tmp/ps1-bug-check)
  --scene-label LABEL   Override expected scene label
  --frame-number N      Override expected frame number
  --no-open             Do not open the HTML report automatically
  -h, --help            Show this help

Examples:
  ./scripts/capture-and-check-ps1.sh --expected-root host-script-review/fishing1 --scene "FISHING 1" --frame-number 80 --actual-frame 1200
  ./scripts/capture-and-check-ps1.sh --expected-root host-script-review/mary1 --scene "MARY 1" --frame-number 50 --actual-frame 3600
  ./scripts/capture-and-check-ps1.sh --expected-root host-references-test4/FISHING-1 --image host-references-test4/FISHING-1/frame_00081.bmp
USAGE
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --expected-root) EXPECTED_ROOT="$2"; shift 2 ;;
        --lookup-root) LOOKUP_ROOT="$2"; shift 2 ;;
        --image) IMAGE_PATH="$2"; shift 2 ;;
        --scene) SCENE_SPEC="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --interval) INTERVAL="$2"; shift 2 ;;
        --actual-frame) ACTUAL_FRAME="$2"; shift 2 ;;
        --no-baseline-mask) USE_BASELINE_MASK=0; shift ;;
        --live) MODE="live"; shift ;;
        --wait) WAIT_TIME="$2"; shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --scene-label) SCENE_LABEL="$2"; shift 2 ;;
        --frame-number) FRAME_NUMBER="$2"; shift 2 ;;
        --no-open) OPEN_REPORT=0; shift ;;
        -h|--help) usage ;;
        *) BOOT_ARGS+=("$1"); shift ;;
    esac
done

if [ -z "$EXPECTED_ROOT" ]; then
    echo "ERROR: --expected-root is required." >&2
    usage
fi

if [ ! -e "$EXPECTED_ROOT" ]; then
    echo "ERROR: expected root not found: $EXPECTED_ROOT" >&2
    exit 1
fi

if [ -z "$LOOKUP_ROOT" ]; then
    LOOKUP_ROOT="$EXPECTED_ROOT"
fi

if [ "$MODE" = "headless" ] && [ -n "$FRAME_NUMBER" ] && [ -z "$ACTUAL_FRAME" ] && [ -z "$IMAGE_PATH" ]; then
    echo "ERROR: headless checks with --frame-number require --actual-frame so the captured PNG matches the expected truth checkpoint." >&2
    exit 1
fi

LOG_FILE="$(mktemp /tmp/ps1-capture-check-log-XXXXXX.txt)"
trap 'rm -f "$LOG_FILE"' EXIT

if [ -n "$IMAGE_PATH" ]; then
    SCREENSHOT_PATH="$IMAGE_PATH"
    BASELINE_IMAGE_PATH=""
else
    if [ "$MODE" = "headless" ]; then
        if [ -z "$SCENE_SPEC" ]; then
            echo "ERROR: --scene is required for headless capture." >&2
            exit 1
        fi

        REGTEST_OUT_DIR="$OUT_DIR/regtest"
        BASELINE_OUT_DIR="$OUT_DIR/regtest-mask"
        REGTEST_CMD_BASE=(./scripts/regtest-scene.sh
            --scene "$SCENE_SPEC"
            --frames "$FRAMES"
            --interval "$INTERVAL")
        if [ "${#BOOT_ARGS[@]}" -gt 0 ]; then
            REGTEST_BOOT="${BOOT_ARGS[*]}"
            REGTEST_CMD_BASE+=(--boot "$REGTEST_BOOT")
        fi

        BASELINE_IMAGE_PATH=""
        if [ "$USE_BASELINE_MASK" -eq 1 ]; then
            REGTEST_MASK_CMD=("${REGTEST_CMD_BASE[@]}"
                --output "$BASELINE_OUT_DIR"
                --overlay-mask)

            echo "=== PS1 headless baseline capture ==="
            printf 'Running:'
            printf ' %q' "${REGTEST_MASK_CMD[@]}"
            printf '\n'

            "${REGTEST_MASK_CMD[@]}" | tee "$LOG_FILE"

            if [ ! -f "$BASELINE_OUT_DIR/result.json" ]; then
                echo "ERROR: headless baseline regtest did not produce result.json." >&2
                exit 1
            fi

            BASELINE_FRAMES_DIR="$(python3 - <<'PY' "$BASELINE_OUT_DIR/result.json"
import json, sys
from pathlib import Path
payload = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
print((payload.get("paths") or {}).get("frames_dir") or "")
PY
)"
            if [ -z "$BASELINE_FRAMES_DIR" ] || [ ! -d "$BASELINE_FRAMES_DIR" ]; then
                echo "ERROR: headless baseline regtest did not produce a frames directory." >&2
                exit 1
            fi

            if [ -n "$ACTUAL_FRAME" ]; then
                BASELINE_IMAGE_PATH="$BASELINE_FRAMES_DIR/frame_$(printf '%05d' "$ACTUAL_FRAME").png"
            else
                BASELINE_IMAGE_PATH="$(find "$BASELINE_FRAMES_DIR" -maxdepth 1 -type f -name 'frame_*.png' | sort | tail -1)"
            fi
            if [ -z "$BASELINE_IMAGE_PATH" ] || [ ! -f "$BASELINE_IMAGE_PATH" ]; then
                echo "ERROR: headless baseline regtest did not produce a usable frame PNG." >&2
                exit 1
            fi
        fi

        REGTEST_CMD=("${REGTEST_CMD_BASE[@]}"
            --output "$REGTEST_OUT_DIR"
            --overlay)

        echo "=== PS1 headless capture ==="
        printf 'Running:'
        printf ' %q' "${REGTEST_CMD[@]}"
        printf '\n'

        "${REGTEST_CMD[@]}" | tee "$LOG_FILE"

        if [ ! -f "$REGTEST_OUT_DIR/result.json" ]; then
            echo "ERROR: headless regtest did not produce result.json." >&2
            exit 1
        fi

        FRAMES_DIR="$(python3 - <<'PY' "$REGTEST_OUT_DIR/result.json"
import json, sys
from pathlib import Path
payload = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
print((payload.get("paths") or {}).get("frames_dir") or "")
PY
)"
        if [ -z "$FRAMES_DIR" ] || [ ! -d "$FRAMES_DIR" ]; then
            echo "ERROR: headless regtest did not produce a frames directory." >&2
            exit 1
        fi

        if [ -n "$ACTUAL_FRAME" ]; then
            SCREENSHOT_PATH="$FRAMES_DIR/frame_$(printf '%05d' "$ACTUAL_FRAME").png"
        else
            SCREENSHOT_PATH="$(find "$FRAMES_DIR" -maxdepth 1 -type f -name 'frame_*.png' | sort | tail -1)"
        fi
        if [ -z "$SCREENSHOT_PATH" ] || [ ! -f "$SCREENSHOT_PATH" ]; then
            echo "ERROR: headless regtest did not produce a usable frame PNG." >&2
            exit 1
        fi
    else
        AUTO_CMD=(./scripts/auto-test-ps1.sh "$WAIT_TIME" --overlay)
        if [ "${#BOOT_ARGS[@]}" -gt 0 ]; then
            AUTO_CMD+=("${BOOT_ARGS[@]}")
        fi

        echo "=== PS1 live capture ==="
        printf 'Running:'
        printf ' %q' "${AUTO_CMD[@]}"
        printf '\n'

        "${AUTO_CMD[@]}" | tee "$LOG_FILE"

        SCREENSHOT_PATH="$(sed -n 's/^SCREENSHOT_PATH=//p' "$LOG_FILE" | tail -1)"
        if [ -z "$SCREENSHOT_PATH" ] || [ ! -f "$SCREENSHOT_PATH" ]; then
            echo "ERROR: could not find captured screenshot in auto-test output." >&2
            exit 1
        fi
    fi
fi

if [ ! -f "$SCREENSHOT_PATH" ]; then
    echo "ERROR: screenshot not found: $SCREENSHOT_PATH" >&2
    exit 1
fi

    CHECK_CMD=(python3 ./scripts/check-character-screenshot.py
        --image "$SCREENSHOT_PATH"
        --expected-root "$EXPECTED_ROOT"
        --lookup-root "$LOOKUP_ROOT"
        --out-dir "$OUT_DIR")

    if [ -n "${BASELINE_IMAGE_PATH:-}" ]; then
        CHECK_CMD+=(--baseline-image "$BASELINE_IMAGE_PATH")
    fi

if [ -n "$SCENE_LABEL" ]; then
    CHECK_CMD+=(--scene-label "$SCENE_LABEL")
fi

if [ -n "$FRAME_NUMBER" ]; then
    CHECK_CMD+=(--frame-number "$FRAME_NUMBER")
fi

echo
echo "=== Character check ==="
printf 'Running:'
printf ' %q' "${CHECK_CMD[@]}"
printf '\n'

"${CHECK_CMD[@]}"

REPORT_HTML="$OUT_DIR/character-truth-report.html"
if [ "$OPEN_REPORT" -eq 1 ] && [ -f "$REPORT_HTML" ] && command -v xdg-open >/dev/null 2>&1; then
    xdg-open "$REPORT_HTML" >/dev/null 2>&1 &
fi

echo
echo "Report: $REPORT_HTML"
