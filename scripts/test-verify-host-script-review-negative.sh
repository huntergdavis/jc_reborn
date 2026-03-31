#!/bin/bash
# test-verify-host-script-review-negative.sh — prove the read-only verifier rejects tampered artifacts.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_DIR="$PROJECT_ROOT/host-script-review"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/host-review-neg.XXXXXX")"

cleanup() {
    rm -rf "$TMP_DIR"
}

trap cleanup EXIT

cp -a "$SOURCE_DIR/." "$TMP_DIR/"

python3 - "$TMP_DIR" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1]) / "manifest.json"
payload = json.loads(path.read_text(encoding="utf-8"))
payload["tampered"] = True
path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
PY

set +e
"$SCRIPT_DIR/verify-host-script-review.sh" --root "$TMP_DIR" --no-head-match >"$TMP_DIR/verify.out" 2>&1
rc=$?
set -e

cat "$TMP_DIR/verify.out"

if [ "$rc" -eq 0 ]; then
    echo "ERROR: verifier unexpectedly passed on tampered artifacts" >&2
    exit 1
fi

if ! grep -q "artifact input hashes do not match summary" "$TMP_DIR/verify.out"; then
    echo "ERROR: verifier failed for an unexpected reason" >&2
    exit 1
fi

echo "negative verification test passed"
