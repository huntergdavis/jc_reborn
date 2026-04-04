#!/bin/bash
# compare-real-shot.sh — Compare a real host or DuckStation screenshot against
# a host reference metadata set using the shared normalized scene surface.

set -euo pipefail

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

IMAGE=""
REFERENCE=""
JSON=0

usage() {
    cat <<'USAGE'
Usage: compare-real-shot.sh --image FILE --reference DIR|FILE [--json]

Options:
  --image FILE       Raw host or DuckStation screenshot
  --reference PATH   Host reference directory or metadata.json
  --json             Emit machine-readable JSON
  -h, --help         Show this help
USAGE
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --image) IMAGE="$2"; shift 2 ;;
        --reference) REFERENCE="$2"; shift 2 ;;
        --json) JSON=1; shift ;;
        -h|--help) usage ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if [ -z "$IMAGE" ] || [ -z "$REFERENCE" ]; then
    usage
fi

CMD=(python3 "$PROJECT_ROOT/scripts/compare-scene-reference.py"
    --result-frame "$IMAGE"
    --reference "$REFERENCE")

if [ "$JSON" -eq 1 ]; then
    CMD+=(--json)
fi

"${CMD[@]}"
