#!/bin/bash
# verify-host-script-review.sh — read-only integrity check for host-script-review artifacts.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT_DIR="$PROJECT_ROOT/host-script-review"
REQUIRE_HEAD_MATCH=1

usage() {
    cat <<'USAGE'
Usage: verify-host-script-review.sh [OPTIONS]

Options:
  --root DIR          Review directory to verify (default: host-script-review)
  --no-head-match     Do not require verification-summary git_head to match current HEAD
  -h, --help          Show this help
USAGE
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --root) ROOT_DIR="$2"; shift 2 ;;
        --no-head-match) REQUIRE_HEAD_MATCH=0; shift ;;
        -h|--help) usage ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

python3 - "$ROOT_DIR" "$PROJECT_ROOT" "$REQUIRE_HEAD_MATCH" <<'PY'
import hashlib
import json
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
project_root = Path(sys.argv[2])
require_head_match = sys.argv[3] == "1"

required = [
    "manifest.json",
    "semantic-truth.json",
    "identification-selfcheck.json",
    "identification-eval.json",
    "expectations.json",
    "host-truth-baseline.json",
    "expectation-report.json",
    "host-truth-compare.json",
    "repro-compare.json",
    "verification-summary.json",
]

missing = [name for name in required if not (root / name).is_file()]
if missing:
    raise SystemExit(f"missing required files: {', '.join(missing)}")

summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))

identify = json.loads((root / "identification-selfcheck.json").read_text(encoding="utf-8"))
for row in identify.get("rows", []):
    best = row.get("best_match") or {}
    query = row.get("query_scene_label")
    if row.get("identification_status") != "identified":
        raise SystemExit(f"identification selfcheck status mismatch for {query}: {row.get('identification_status')}")
    if best.get("scene_label") != query:
        raise SystemExit(f"identification selfcheck best-match mismatch for {query}")
    if not best.get("exact_scene_signature"):
        raise SystemExit(f"identification selfcheck signature mismatch for {query}")
print("identification-selfcheck: ok")

identify_eval = json.loads((root / "identification-eval.json").read_text(encoding="utf-8"))
if not identify_eval.get("passed"):
    raise SystemExit(
        "identification-eval failed: " + "; ".join(identify_eval.get("failures", []))
    )
print(
    "identification-eval: ok "
    f"min_margin={identify_eval.get('min_identified_margin')} "
    f"max_nonmatch_score={identify_eval.get('max_nonmatch_score')}"
)

for name in ("expectation-report", "host-truth-compare", "repro-compare"):
    path = root / f"{name}.json"
    payload = json.loads(path.read_text(encoding="utf-8"))
    mismatch_count = int(payload.get("mismatch_count", -1))
    print(f"{name}: mismatch_count={mismatch_count}")
    if mismatch_count != 0:
        raise SystemExit(f"{name} mismatch_count must be 0")
    expected = (((summary.get("checks") or {}).get(name) or {}).get("mismatch_count"))
    if expected != mismatch_count:
        raise SystemExit(f"summary mismatch for {name}: expected {expected}, got {mismatch_count}")

selfcheck_summary = ((summary.get("checks") or {}).get("identification-selfcheck") or {})
if not selfcheck_summary.get("present") or not selfcheck_summary.get("passed"):
    raise SystemExit("summary reports identification-selfcheck failure")
if int(selfcheck_summary.get("row_count", 0)) != len(identify.get("rows", [])):
    raise SystemExit("summary row_count mismatch for identification-selfcheck")

eval_summary = ((summary.get("checks") or {}).get("identification-eval") or {})
if not eval_summary.get("present") or not eval_summary.get("passed"):
    raise SystemExit("summary reports identification-eval failure")
if int(eval_summary.get("scene_count", 0)) != int(identify_eval.get("scene_count", 0)):
    raise SystemExit("summary scene_count mismatch for identification-eval")
if eval_summary.get("min_identified_margin") != identify_eval.get("min_identified_margin"):
    raise SystemExit("summary min_identified_margin mismatch for identification-eval")
if eval_summary.get("max_nonmatch_score") != identify_eval.get("max_nonmatch_score"):
    raise SystemExit("summary max_nonmatch_score mismatch for identification-eval")

artifact_inputs = summary.get("artifact_inputs") or {}
if not artifact_inputs:
    raise SystemExit("verification-summary.json missing artifact_inputs")

computed_inputs = {}
for name in sorted(artifact_inputs):
    path = root / name
    if not path.is_file():
        raise SystemExit(f"artifact input missing: {name}")
    computed_inputs[name] = hashlib.sha256(path.read_bytes()).hexdigest()

if computed_inputs != artifact_inputs:
    raise SystemExit(
        "artifact input hashes do not match summary"
    )

digest_payload = json.dumps(
    {
        "git_head": summary.get("git_head"),
        "verify_repro": summary.get("verify_repro"),
        "checks": summary.get("checks"),
        "artifacts": computed_inputs,
    },
    sort_keys=True,
    separators=(",", ":"),
).encode("utf-8")
computed_digest = hashlib.sha256(digest_payload).hexdigest()
print(f"artifact_sha256: {computed_digest}")
if computed_digest != summary.get("artifact_sha256"):
    raise SystemExit("artifact_sha256 does not match summary")

if not summary.get("all_passed"):
    raise SystemExit("verification-summary.json reports all_passed=false")

if require_head_match:
    current_head = subprocess.run(
        ["git", "-C", str(project_root), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    print(f"summary git_head: {summary.get('git_head')}")
    print(f"current git_head: {current_head}")
    if current_head != summary.get("git_head"):
        raise SystemExit("verification-summary.json git_head does not match current HEAD")

print("host-script-review verification passed")
PY
