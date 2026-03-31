#!/bin/bash
# test-verify-host-script-review-head-mismatch.sh — prove the verifier rejects stale summary provenance.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_DIR="$PROJECT_ROOT/host-script-review"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/host-review-head.XXXXXX")"

cleanup() {
    rm -rf "$TMP_DIR"
}

trap cleanup EXIT

cp -a "$SOURCE_DIR/." "$TMP_DIR/"

python3 - "$TMP_DIR" <<'PY'
import json
import hashlib
import sys
from pathlib import Path

path = Path(sys.argv[1]) / "verification-summary.json"
payload = json.loads(path.read_text(encoding="utf-8"))
payload["git_head"] = "deadbeef" * 5
payload["git_head_short"] = "deadbeef"
digest_payload = json.dumps(
    {
        "git_head": payload.get("git_head"),
        "verify_repro": payload.get("verify_repro"),
        "checks": payload.get("checks"),
        "artifacts": payload.get("artifact_inputs"),
    },
    sort_keys=True,
    separators=(",", ":"),
).encode("utf-8")
payload["artifact_sha256"] = hashlib.sha256(digest_payload).hexdigest()
path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
PY

set +e
"$SCRIPT_DIR/verify-host-script-review.sh" --root "$TMP_DIR" >"$TMP_DIR/verify.out" 2>&1
rc=$?
set -e

cat "$TMP_DIR/verify.out"

if [ "$rc" -eq 0 ]; then
    echo "ERROR: verifier unexpectedly passed on stale summary provenance" >&2
    exit 1
fi

if ! grep -q "git_head does not match current HEAD" "$TMP_DIR/verify.out"; then
    echo "ERROR: verifier failed for an unexpected reason" >&2
    exit 1
fi

echo "head-mismatch verification test passed"
