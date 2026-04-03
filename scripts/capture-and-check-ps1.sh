#!/bin/bash
# capture-and-check-ps1.sh — Run an overlay-backed PS1 capture, compare the
# resulting screenshot against expected character truth, and open the HTML diff.

set -euo pipefail

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

WAIT_TIME=35
EXPECTED_ROOT=""
LOOKUP_ROOT=""
OUT_DIR="/tmp/ps1-bug-check"
SCENE_LABEL=""
FRAME_NUMBER=""
OPEN_REPORT=1
IMAGE_PATH=""
BOOT_ARGS=()

usage() {
    cat <<'USAGE'
Usage: capture-and-check-ps1.sh --expected-root PATH [OPTIONS] [BOOTMODE...]

Options:
  --expected-root PATH  Expected scene/corpus root used to build truth
  --lookup-root PATH    Root used to resolve overlay sprite hashes (default: expected root)
  --image PATH          Reuse an existing overlay screenshot instead of launching DuckStation
  --wait N              Seconds before first screenshot (default: 35)
  --out-dir PATH        Output directory for diff artifacts (default: /tmp/ps1-bug-check)
  --scene-label LABEL   Override expected scene label
  --frame-number N      Override expected frame number
  --no-open             Do not open the HTML report automatically
  -h, --help            Show this help

Examples:
  ./scripts/capture-and-check-ps1.sh --expected-root host-script-review/fishing1 "story scene 17"
  ./scripts/capture-and-check-ps1.sh --expected-root host-script-review/mary1 --scene-label "MARY 1" "story scene 61"
  ./scripts/capture-and-check-ps1.sh --expected-root host-references-test4/FISHING-1 --image host-references-test4/FISHING-1/frame_00081.bmp
USAGE
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --expected-root) EXPECTED_ROOT="$2"; shift 2 ;;
        --lookup-root) LOOKUP_ROOT="$2"; shift 2 ;;
        --image) IMAGE_PATH="$2"; shift 2 ;;
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

LOG_FILE="$(mktemp /tmp/ps1-capture-check-log-XXXXXX.txt)"
trap 'rm -f "$LOG_FILE"' EXIT

if [ -n "$IMAGE_PATH" ]; then
    SCREENSHOT_PATH="$IMAGE_PATH"
else
    AUTO_CMD=(./scripts/auto-test-ps1.sh "$WAIT_TIME" --overlay)
    if [ "${#BOOT_ARGS[@]}" -gt 0 ]; then
        AUTO_CMD+=("${BOOT_ARGS[@]}")
    fi

    echo "=== PS1 capture ==="
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

if [ ! -f "$SCREENSHOT_PATH" ]; then
    echo "ERROR: screenshot not found: $SCREENSHOT_PATH" >&2
    exit 1
fi

CHECK_CMD=(python3 ./scripts/check-character-screenshot.py
    --image "$SCREENSHOT_PATH"
    --expected-root "$EXPECTED_ROOT"
    --lookup-root "$LOOKUP_ROOT"
    --out-dir "$OUT_DIR")

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
