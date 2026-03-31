#!/bin/bash
# refresh-host-script-review.sh — rebuild the deterministic host script-review set
# and verify strict expectation and reproducibility contracts.

set -euo pipefail

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run this script as root/sudo." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="$PROJECT_ROOT/host-script-review"
TMP_DIR="$PROJECT_ROOT/host-script-review-verify"
VERIFY_REPRO=1

assert_clean_tracked_inputs() {
    local status
    status="$(git -C "$PROJECT_ROOT" status --short --untracked-files=no -- \
        host-script-review/expectations.json \
        host-script-review/host-truth-baseline.json \
        host-script-review/expectation-report.json \
        host-script-review/host-truth-compare.json \
        host-script-review/repro-compare.json \
        host-script-review/verification-summary.json \
        scripts/capture-host-scene.sh \
        scripts/compile-host-semantic-truth.py \
        scripts/compare-host-script-vs-expectations.py \
        scripts/evaluate-host-identification.py \
        scripts/evaluate-host-identification-partials.py \
        scripts/identify-host-scene.py \
        scripts/render-host-expectation-report.py \
        scripts/render-host-repro-compare.py \
        scripts/render-host-script-index.py \
        scripts/generate-host-truth-baseline.py \
        scripts/refresh-host-script-review.sh)"
    if [ -n "$status" ]; then
        echo "ERROR: tracked host-review inputs are dirty; commit or stash them before refresh." >&2
        echo "$status" >&2
        exit 1
    fi
}

cleanup_capture_processes() {
    pkill -f "$PROJECT_ROOT/scripts/capture-host-scene.sh" >/dev/null 2>&1 || true
    pkill -f "$PROJECT_ROOT/build-host/jc_reborn" >/dev/null 2>&1 || true
    pkill -f '/usr/bin/xvfb-run -a env SDL_AUDIODRIVER=dummy' >/dev/null 2>&1 || true
    python3 - <<'PY'
import os
import signal
import subprocess

ps = subprocess.run(
    ["ps", "-u", os.environ["USER"], "-o", "pid=", "-o", "args="],
    check=True,
    capture_output=True,
    text=True,
)
for raw in ps.stdout.splitlines():
    line = raw.strip()
    if not line:
        continue
    pid_text, _, args = line.partition(" ")
    args = args.strip()
    if "Xvfb :" in args and "/tmp/xvfb-run." in args:
        try:
            os.kill(int(pid_text), signal.SIGTERM)
        except ProcessLookupError:
            pass
PY
}

usage() {
    cat <<'USAGE'
Usage: refresh-host-script-review.sh [OPTIONS]

Options:
  --output DIR       Output review directory (default: host-script-review)
  --tmp DIR          Temporary verification directory (default: host-script-review-verify)
  --no-repro         Skip second-pass reproducibility verification
  -h, --help         Show this help
USAGE
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --output) OUT_DIR="$2"; shift 2 ;;
        --tmp) TMP_DIR="$2"; shift 2 ;;
        --no-repro) VERIFY_REPRO=0; shift ;;
        -h|--help) usage ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

run_with_timeout() {
    local seconds="$1"; shift
    cleanup_capture_processes
    set +e
    timeout "$seconds" "$@"
    local rc=$?
    set -e
    if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
        return "$rc"
    fi
}

capture_review_set() {
    local root="$1"

    rm -rf "$root"
    mkdir -p "$root"

    run_with_timeout 60 \
        "$SCRIPT_DIR/capture-host-scene.sh" \
        --scene "FISHING 1" \
        --mode story-direct \
        --frames 80 \
        --interval 20 \
        --output "$root/fishing1" \
        --no-stamp

    run_with_timeout 90 \
        "$SCRIPT_DIR/capture-host-scene.sh" \
        --scene "MARY 1" \
        --mode story-direct \
        --frames 250 \
        --interval 50 \
        --output "$root/mary1" \
        --no-stamp

    python3 "$SCRIPT_DIR/render-host-script-index.py" \
        --root "$root" \
        --out-json "$root/manifest.json" \
        --out-html "$root/index.html" \
        --title "Host Script Review Index"

    python3 "$SCRIPT_DIR/compile-host-semantic-truth.py" \
        --root "$root" \
        --out-json "$root/semantic-truth.json"

    python3 "$SCRIPT_DIR/identify-host-scene.py" \
        --database-json "$root/semantic-truth.json" \
        --query-json "$root/semantic-truth.json" \
        --out-json "$root/identification-selfcheck.json"

    python3 "$SCRIPT_DIR/evaluate-host-identification.py" \
        --report-json "$root/identification-selfcheck.json" \
        --out-json "$root/identification-eval.json"

    python3 "$SCRIPT_DIR/evaluate-host-identification-partials.py" \
        --semantic-json "$root/semantic-truth.json" \
        --out-json "$root/identification-partials.json"

    python3 "$SCRIPT_DIR/generate-host-truth-baseline.py" \
        --manifest-json "$root/manifest.json" \
        --out-json "$root/host-truth-baseline.json"

    cp "$root/host-truth-baseline.json" "$root/expectations.json"

    python3 "$SCRIPT_DIR/compare-host-script-vs-expectations.py" \
        --manifest-json "$root/manifest.json" \
        --expectations-json "$root/expectations.json" \
        --out-json "$root/expectation-report.json"

    python3 "$SCRIPT_DIR/render-host-expectation-report.py" \
        --report-json "$root/expectation-report.json" \
        --out-html "$root/expectation-report.html" \
        --title "Host Script Expectation Report"

    python3 "$SCRIPT_DIR/compare-host-script-vs-expectations.py" \
        --manifest-json "$root/manifest.json" \
        --expectations-json "$root/host-truth-baseline.json" \
        --out-json "$root/host-truth-compare.json"

    python3 "$SCRIPT_DIR/render-host-expectation-report.py" \
        --report-json "$root/host-truth-compare.json" \
        --out-html "$root/host-truth-compare.html" \
        --title "Host Truth Self-Check"
}

assert_zero_mismatches() {
    local path="$1"
    local label="$2"
    python3 - "$path" "$label" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
label = sys.argv[2]
data = json.loads(path.read_text(encoding="utf-8"))
count = int(data.get("mismatch_count", 0))
print(f"{label}: mismatch_count={count}")
if count != 0:
    raise SystemExit(1)
PY
}

assert_identification_selfcheck() {
    local path="$1"
    python3 - "$path" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
data = json.loads(path.read_text(encoding="utf-8"))
rows = data.get("rows", [])
if not rows:
    raise SystemExit("identification selfcheck has no rows")
for row in rows:
    best = row.get("best_match") or {}
    query = row.get("query_scene_label")
    if row.get("identification_status") != "identified":
        raise SystemExit(f"identification status not identified for {query}: {row.get('identification_status')}")
    if best.get("scene_label") != query:
        raise SystemExit(f"best match mismatch for {query}: got {best.get('scene_label')}")
    if not best.get("exact_scene_signature"):
        raise SystemExit(f"scene signature mismatch for {query}")
print("identification-selfcheck: ok")
PY
}

assert_identification_eval() {
    local path="$1"
    python3 - "$path" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
data = json.loads(path.read_text(encoding="utf-8"))
if not data.get("passed"):
    raise SystemExit("identification evaluation failed: " + "; ".join(data.get("failures", [])))
print(
    "identification-eval: ok "
    f"min_margin={data.get('min_identified_margin')} "
    f"max_nonmatch_score={data.get('max_nonmatch_score')} "
    f"min_ratio={data.get('min_best_to_second_ratio')}"
)
PY
}

assert_identification_partials() {
    local path="$1"
    python3 - "$path" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
data = json.loads(path.read_text(encoding="utf-8"))
if not data.get("passed"):
    raise SystemExit("partial identification evaluation failed: " + "; ".join(data.get("failures", [])))
print(
    "identification-partials: ok "
    f"query_count={data.get('query_count')} "
    f"min_margin={data.get('min_margin')} "
    f"min_ratio={data.get('min_best_to_second_ratio')}"
)
PY
}

write_verification_summary() {
    local root="$1"
    local git_head git_head_short
    git_head="$(git -C "$PROJECT_ROOT" rev-parse HEAD)"
    git_head_short="$(git -C "$PROJECT_ROOT" rev-parse --short HEAD)"
    python3 - "$root" "$git_head" "$git_head_short" "$VERIFY_REPRO" <<'PY'
import json
import hashlib
import sys
from datetime import datetime, timezone
from pathlib import Path

root = Path(sys.argv[1])
git_head = sys.argv[2]
git_head_short = sys.argv[3]
verify_repro = sys.argv[4] == "1"

checks = {}
for name in ("expectation-report", "host-truth-compare", "repro-compare"):
    path = root / f"{name}.json"
    if not path.is_file():
        checks[name] = {"present": False, "mismatch_count": None}
        continue
    payload = json.loads(path.read_text(encoding="utf-8"))
    checks[name] = {
        "present": True,
        "mismatch_count": int(payload.get("mismatch_count", 0)),
    }

identify_selfcheck_path = root / "identification-selfcheck.json"
identify_selfcheck = {
    "present": identify_selfcheck_path.is_file(),
    "passed": False,
    "row_count": 0,
}
if identify_selfcheck_path.is_file():
    payload = json.loads(identify_selfcheck_path.read_text(encoding="utf-8"))
    rows = payload.get("rows", [])
    identify_selfcheck["row_count"] = len(rows)
    identify_selfcheck["passed"] = bool(rows) and all(
        row.get("identification_status") == "identified"
        and ((row.get("best_match") or {}).get("scene_label") == row.get("query_scene_label"))
        and bool((row.get("best_match") or {}).get("exact_scene_signature"))
        for row in rows
    )
checks["identification-selfcheck"] = identify_selfcheck

identify_eval_path = root / "identification-eval.json"
identify_eval = {
    "present": identify_eval_path.is_file(),
    "passed": False,
    "scene_count": 0,
    "min_identified_margin": None,
    "max_nonmatch_score": None,
    "min_best_to_second_ratio": None,
}
if identify_eval_path.is_file():
    payload = json.loads(identify_eval_path.read_text(encoding="utf-8"))
    identify_eval["passed"] = bool(payload.get("passed"))
    identify_eval["scene_count"] = int(payload.get("scene_count", 0))
    identify_eval["min_identified_margin"] = payload.get("min_identified_margin")
    identify_eval["max_nonmatch_score"] = payload.get("max_nonmatch_score")
    identify_eval["min_best_to_second_ratio"] = payload.get("min_best_to_second_ratio")
checks["identification-eval"] = identify_eval

identify_partials_path = root / "identification-partials.json"
identify_partials = {
    "present": identify_partials_path.is_file(),
    "passed": False,
    "query_count": 0,
    "min_margin": None,
    "min_best_to_second_ratio": None,
}
if identify_partials_path.is_file():
    payload = json.loads(identify_partials_path.read_text(encoding="utf-8"))
    identify_partials["passed"] = bool(payload.get("passed"))
    identify_partials["query_count"] = int(payload.get("query_count", 0))
    identify_partials["min_margin"] = payload.get("min_margin")
    identify_partials["min_best_to_second_ratio"] = payload.get("min_best_to_second_ratio")
checks["identification-partials"] = identify_partials

digest_inputs = {}
for name in (
    "manifest.json",
    "semantic-truth.json",
    "identification-selfcheck.json",
    "identification-eval.json",
    "identification-partials.json",
    "expectations.json",
    "host-truth-baseline.json",
    "expectation-report.json",
    "host-truth-compare.json",
    "repro-compare.json",
):
    path = root / name
    if path.is_file():
        digest_inputs[name] = hashlib.sha256(path.read_bytes()).hexdigest()

digest_payload = json.dumps(
    {
        "git_head": git_head,
        "verify_repro": verify_repro,
        "checks": checks,
        "artifacts": digest_inputs,
    },
    sort_keys=True,
    separators=(",", ":"),
).encode("utf-8")

summary = {
    "generated_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    "git_head": git_head,
    "git_head_short": git_head_short,
    "verify_repro": verify_repro,
    "checks": checks,
    "artifact_sha256": hashlib.sha256(digest_payload).hexdigest(),
    "artifact_inputs": digest_inputs,
    "all_passed": (
        checks["identification-selfcheck"]["present"]
        and checks["identification-selfcheck"]["passed"]
        and checks["identification-eval"]["present"]
        and checks["identification-eval"]["passed"]
        and checks["identification-partials"]["present"]
        and checks["identification-partials"]["passed"]
        and checks["expectation-report"]["present"]
        and checks["expectation-report"]["mismatch_count"] == 0
        and checks["host-truth-compare"]["present"]
        and checks["host-truth-compare"]["mismatch_count"] == 0
        and (
            (not verify_repro)
            or (
                checks["repro-compare"]["present"]
                and checks["repro-compare"]["mismatch_count"] == 0
            )
        )
    ),
}

(root / "verification-summary.json").write_text(
    json.dumps(summary, indent=2) + "\n",
    encoding="utf-8",
)

(root / "verification-summary.txt").write_text(
    "status={status} git={git_head_short} digest={digest} "
    "identify-selfcheck={identify_selfcheck} identify-eval={identify_eval} identify-partials={identify_partials} "
    "identify-ratio={identify_ratio} "
    "expectation-report={expectation} host-truth-compare={host_truth} repro-compare={repro}\n".format(
        status="PASS" if summary["all_passed"] else "FAIL",
        git_head_short=git_head_short,
        digest=summary["artifact_sha256"],
        identify_selfcheck="ok" if checks["identification-selfcheck"]["passed"] else "fail",
        identify_eval="ok" if checks["identification-eval"]["passed"] else "fail",
        identify_partials="ok" if checks["identification-partials"]["passed"] else "fail",
        identify_ratio=checks["identification-eval"]["min_best_to_second_ratio"],
        expectation=checks["expectation-report"]["mismatch_count"],
        host_truth=checks["host-truth-compare"]["mismatch_count"],
        repro=checks["repro-compare"]["mismatch_count"],
    ),
    encoding="utf-8",
)
PY
}

"$SCRIPT_DIR/build-host.sh"

trap cleanup_capture_processes EXIT
cleanup_capture_processes
assert_clean_tracked_inputs

capture_review_set "$OUT_DIR"
assert_zero_mismatches "$OUT_DIR/expectation-report.json" "expectation-report"
assert_zero_mismatches "$OUT_DIR/host-truth-compare.json" "host-truth-compare"
assert_identification_selfcheck "$OUT_DIR/identification-selfcheck.json"
assert_identification_eval "$OUT_DIR/identification-eval.json"
assert_identification_partials "$OUT_DIR/identification-partials.json"

if [ "$VERIFY_REPRO" -eq 1 ]; then
    rm -rf "$TMP_DIR"
    mkdir -p "$TMP_DIR/a" "$TMP_DIR/b"
    capture_review_set "$TMP_DIR/a"
    capture_review_set "$TMP_DIR/b"

    python3 "$SCRIPT_DIR/compare-host-manifests.py" \
        --base-json "$TMP_DIR/a/manifest.json" \
        --other-json "$TMP_DIR/b/manifest.json" \
        --out-json "$OUT_DIR/repro-compare.json"

    python3 "$SCRIPT_DIR/render-host-repro-compare.py" \
        --report-json "$OUT_DIR/repro-compare.json" \
        --out-html "$OUT_DIR/repro-compare.html" \
        --title "Host Script Reproducibility Report"

    assert_zero_mismatches "$OUT_DIR/repro-compare.json" "repro-compare"
fi

write_verification_summary "$OUT_DIR"

rm -rf "$TMP_DIR"

echo "Host script review refreshed in $OUT_DIR"
