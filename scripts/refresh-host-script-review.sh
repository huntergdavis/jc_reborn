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
        host-script-review/frame-image-regression-baseline.json \
        host-script-review/frame-image-regression-report.json \
        host-script-review/frame-meta-regression-baseline.json \
        host-script-review/frame-meta-regression-report.json \
        host-script-review/capture-regression-report.json \
        host-script-review/capture-regression-review.html \
        host-script-review/identification-regression-floors.json \
        host-script-review/semantic-regression-baseline.json \
        host-script-review/semantic-regression-report.json \
        host-script-review/verification-summary.json \
        scripts/capture-host-scene.sh \
        scripts/compile-host-semantic-truth.py \
        scripts/compare-host-script-vs-expectations.py \
        scripts/evaluate-frame-image-regression.py \
        scripts/evaluate-frame-meta-regression.py \
        scripts/evaluate-semantic-regression.py \
        scripts/render-capture-regression-report.py \
        scripts/evaluate-host-identification.py \
        scripts/evaluate-host-identification-challenges.py \
        scripts/evaluate-host-identification-partials.py \
        scripts/evaluate-host-identification-temporal.py \
        scripts/identify-host-scene.py \
        scripts/render-host-expectation-report.py \
        scripts/render-host-identification-report.py \
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
    local floors_path="$root/identification-regression-floors.json"
    local semantic_baseline_path="$root/semantic-regression-baseline.json"
    local floors_tmp=""
    local semantic_baseline_tmp=""

    if [ -f "$floors_path" ]; then
        floors_tmp="$(mktemp)"
        cp "$floors_path" "$floors_tmp"
    fi
    if [ -f "$semantic_baseline_path" ]; then
        semantic_baseline_tmp="$(mktemp)"
        cp "$semantic_baseline_path" "$semantic_baseline_tmp"
    fi

    rm -rf "$root"
    mkdir -p "$root"

    if [ -n "$floors_tmp" ]; then
        mv "$floors_tmp" "$floors_path"
    fi
    if [ -n "$semantic_baseline_tmp" ]; then
        mv "$semantic_baseline_tmp" "$semantic_baseline_path"
    fi

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

    python3 "$SCRIPT_DIR/evaluate-host-identification-challenges.py" \
        --semantic-json "$root/semantic-truth.json" \
        --out-json "$root/identification-challenges.json"

    python3 "$SCRIPT_DIR/evaluate-host-identification-temporal.py" \
        --semantic-json "$root/semantic-truth.json" \
        --out-json "$root/identification-temporal.json"

    python3 "$SCRIPT_DIR/render-host-identification-report.py" \
        --selfcheck-json "$root/identification-selfcheck.json" \
        --eval-json "$root/identification-eval.json" \
        --partials-json "$root/identification-partials.json" \
        --challenges-json "$root/identification-challenges.json" \
        --temporal-json "$root/identification-temporal.json" \
        --manifest-json "$root/manifest.json" \
        --semantic-truth-json "$root/semantic-truth.json" \
        --out-html "$root/identification-review.html" \
        --title "Host Identification Review"

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

assert_identification_challenges() {
    local path="$1"
    python3 - "$path" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
data = json.loads(path.read_text(encoding="utf-8"))
if not data.get("passed"):
    raise SystemExit("challenge identification evaluation failed: " + "; ".join(data.get("failures", [])))
print(
    "identification-challenges: ok "
    f"query_count={data.get('query_count')} "
    f"max_best_score={data.get('max_best_score')} "
    f"max_margin={data.get('max_margin')} "
    f"ambiguous={data.get('ambiguous_count')} "
    f"unknown={data.get('unknown_count')}"
)
PY
}

assert_identification_temporal() {
    local path="$1"
    python3 - "$path" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
data = json.loads(path.read_text(encoding="utf-8"))
if not data.get("passed"):
    raise SystemExit("temporal identification evaluation failed: " + "; ".join(data.get("failures", [])))
print(
    "identification-temporal: ok "
    f"query_count={data.get('query_count')} "
    f"min_margin={data.get('min_identified_margin')} "
    f"min_ratio={data.get('min_identified_ratio')} "
    f"max_score_drop={data.get('max_score_drop')} "
    f"max_margin_drop={data.get('max_identified_margin_drop')}"
)
PY
}

assert_identification_regression_floors() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
floors = json.loads((root / "identification-regression-floors.json").read_text(encoding="utf-8"))
identify_eval = json.loads((root / "identification-eval.json").read_text(encoding="utf-8"))
identify_partials = json.loads((root / "identification-partials.json").read_text(encoding="utf-8"))
identify_challenges = json.loads((root / "identification-challenges.json").read_text(encoding="utf-8"))
identify_temporal = json.loads((root / "identification-temporal.json").read_text(encoding="utf-8"))

failures = []
if float(identify_eval["min_identified_margin"]) < float(floors["identification-eval"]["min_identified_margin"]):
    failures.append("identification-eval min_identified_margin below floor")
if float(identify_eval["min_best_to_second_ratio"]) < float(floors["identification-eval"]["min_best_to_second_ratio"]):
    failures.append("identification-eval min_best_to_second_ratio below floor")
if float(identify_eval["max_nonmatch_score"]) > float(floors["identification-eval"]["max_nonmatch_score"]):
    failures.append("identification-eval max_nonmatch_score above ceiling")

if float(identify_partials["min_margin"]) < float(floors["identification-partials"]["min_margin"]):
    failures.append("identification-partials min_margin below floor")
if float(identify_partials["min_best_to_second_ratio"]) < float(floors["identification-partials"]["min_best_to_second_ratio"]):
    failures.append("identification-partials min_best_to_second_ratio below floor")

if float(identify_temporal["min_identified_margin"]) < float(floors["identification-temporal"]["min_identified_margin"]):
    failures.append("identification-temporal min_identified_margin below floor")
if float(identify_temporal["min_identified_ratio"]) < float(floors["identification-temporal"]["min_identified_ratio"]):
    failures.append("identification-temporal min_identified_ratio below floor")
if float(identify_temporal["max_score_drop"]) > float(floors["identification-temporal"]["max_score_drop"]):
    failures.append("identification-temporal max_score_drop above ceiling")
if float(identify_temporal["max_identified_margin_drop"]) > float(floors["identification-temporal"]["max_identified_margin_drop"]):
    failures.append("identification-temporal max_identified_margin_drop above ceiling")

if float(identify_challenges["max_best_score"]) > float(floors["identification-challenges"]["max_best_score"]):
    failures.append("identification-challenges max_best_score above ceiling")
if float(identify_challenges["max_margin"]) > float(floors["identification-challenges"]["max_margin"]):
    failures.append("identification-challenges max_margin above ceiling")

if failures:
    raise SystemExit("identification-regression-floors failed: " + "; ".join(failures))

print("identification-regression-floors: ok")
PY
}

assert_frame_meta_regression_baseline() {
    local root="$1"
    python3 "$SCRIPT_DIR/evaluate-frame-meta-regression.py" \
        --baseline "$root/frame-meta-regression-baseline.json" \
        --root "$root" \
        --out-json "$root/frame-meta-regression-report.json"
    python3 - "$root/frame-meta-regression-report.json" <<'PY'
import json
import sys
from pathlib import Path

report = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
if not report.get("passed", False):
    failures = []
    for failure in report.get("failures", []):
        failures.append(f"{failure.get('scene')} {failure.get('frame')} {failure.get('field')} drifted")
    raise SystemExit("frame-meta-regression-baseline failed: " + "; ".join(failures))
print("frame-meta-regression-baseline: ok")
PY
}

assert_frame_image_regression_baseline() {
    local root="$1"
    python3 "$SCRIPT_DIR/evaluate-frame-image-regression.py" \
        --baseline "$root/frame-image-regression-baseline.json" \
        --root "$root" \
        --out-json "$root/frame-image-regression-report.json"
    python3 - "$root/frame-image-regression-report.json" <<'PY'
import json
import sys
from pathlib import Path

report = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
if not report.get("passed", False):
    failures = []
    for failure in report.get("failures", []):
        failures.append(f"{failure.get('scene')} {failure.get('frame')} {failure.get('field')} drifted")
    raise SystemExit("frame-image-regression-baseline failed: " + "; ".join(failures))
print("frame-image-regression-baseline: ok")
PY
}

assert_semantic_regression_baseline() {
    local root="$1"
    python3 "$SCRIPT_DIR/evaluate-semantic-regression.py" \
        --baseline "$root/semantic-regression-baseline.json" \
        --semantic-truth "$root/semantic-truth.json" \
        --out-json "$root/semantic-regression-report.json"
    python3 - "$root/semantic-regression-report.json" <<'PY'
import json
import sys
from pathlib import Path

report = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
if not report.get("passed", False):
    failures = []
    for failure in report.get("failures", []):
        scene = failure.get("scene_label")
        frame = failure.get("frame_number")
        field = failure.get("field")
        if frame is None:
            failures.append(f"{scene} {field} drifted")
        else:
            failures.append(f"{scene} frame {frame} {field} drifted")
    raise SystemExit("semantic-regression-baseline failed: " + "; ".join(failures))
print("semantic-regression-baseline: ok")
PY
}

write_capture_regression_report() {
    local root="$1"
    python3 "$SCRIPT_DIR/render-capture-regression-report.py" \
        --frame-image "$root/frame-image-regression-report.json" \
        --frame-meta "$root/frame-meta-regression-report.json" \
        --semantic "$root/semantic-regression-report.json" \
        --out-json "$root/capture-regression-report.json" \
        --out-html "$root/capture-regression-review.html"
}

assert_capture_regression_report_consistency() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
capture_regression = json.loads((root / "capture-regression-report.json").read_text(encoding="utf-8"))
frame_image_regression = json.loads((root / "frame-image-regression-report.json").read_text(encoding="utf-8"))
frame_meta_regression = json.loads((root / "frame-meta-regression-report.json").read_text(encoding="utf-8"))
semantic_report = json.loads((root / "semantic-regression-report.json").read_text(encoding="utf-8"))

checks = capture_regression.get("checks", {})
totals = capture_regression.get("totals", {})
first_failed = capture_regression.get("first_failed_scenes", {})

def first_failed_scene(report, label_key):
    for scene in report.get("scenes", []):
        if not scene.get("passed", False):
            value = scene.get(label_key)
            return None if value in (None, "") else str(value)
    return None

expected = {
    "frame-image": frame_image_regression,
    "frame-meta": frame_meta_regression,
    "semantic": semantic_report,
}
for key, report in expected.items():
    actual = checks.get(key, {})
    if bool(actual.get("passed", False)) != bool(report.get("passed", False)):
        raise SystemExit(f"capture-regression-report {key} passed mismatch")
    if int(actual.get("failure_count", 0)) != int(report.get("failure_count", 0)):
        raise SystemExit(f"capture-regression-report {key} failure_count mismatch")

if first_failed.get("frame-image") != first_failed_scene(frame_image_regression, "scene"):
    raise SystemExit("capture-regression-report frame-image first_failed_scene mismatch")
if first_failed.get("frame-meta") != first_failed_scene(frame_meta_regression, "scene"):
    raise SystemExit("capture-regression-report frame-meta first_failed_scene mismatch")
if first_failed.get("semantic") != first_failed_scene(semantic_report, "scene_label"):
    raise SystemExit("capture-regression-report semantic first_failed_scene mismatch")

if int(totals.get("frame_image_failures", 0)) != int(frame_image_regression.get("failure_count", 0)):
    raise SystemExit("capture-regression-report frame_image_failures mismatch")
if int(totals.get("frame_meta_failures", 0)) != int(frame_meta_regression.get("failure_count", 0)):
    raise SystemExit("capture-regression-report frame_meta_failures mismatch")
if int(totals.get("semantic_failures", 0)) != int(semantic_report.get("failure_count", 0)):
    raise SystemExit("capture-regression-report semantic_failures mismatch")

print("capture-regression-report: ok")
PY
}

assert_verification_summary_review_paths() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
review_paths = summary.get("review_paths", {})
required = {
    "index_html": root / "index.html",
    "identification_review_html": root / "identification-review.html",
    "capture_regression_review_html": root / "capture-regression-review.html",
}
for key, expected in required.items():
    actual = review_paths.get(key)
    if actual != str(expected.resolve()):
        raise SystemExit(f"verification-summary review_paths.{key} mismatch")
print("verification-summary review_paths: ok")
PY
}

assert_verification_summary_review_root() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
actual = summary.get("review_root")
if actual != str(root):
    raise SystemExit("verification-summary review_root mismatch")
print("verification-summary review_root: ok")
PY
}

assert_verification_summary_absolute_paths() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))

def check_block(name: str, value) -> None:
    if name == "review_root":
        if not isinstance(value, str) or not Path(value).is_absolute():
            raise SystemExit(f"verification-summary {name} must be an absolute path")
        return
    if not name.endswith("_paths"):
        return
    if not isinstance(value, dict):
        raise SystemExit(f"verification-summary {name} must be a path map")
    for key, path_value in value.items():
        if not isinstance(path_value, str) or not Path(path_value).is_absolute():
            raise SystemExit(f"verification-summary {name}.{key} must be an absolute path")

for key, value in summary.items():
    check_block(key, value)

print("verification-summary absolute paths: ok")
PY
}

assert_verification_summary_existing_paths() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))

def check_path(label: str, path_value: str) -> None:
    path = Path(path_value)
    if not path.exists():
        raise SystemExit(f"verification-summary {label} does not exist")

for key, value in summary.items():
    if key == "review_root":
        check_path(key, value)
        continue
    if not key.endswith("_paths"):
        continue
    for path_key, path_value in value.items():
        check_path(f"{key}.{path_key}", path_value)

print("verification-summary existing paths: ok")
PY
}

assert_verification_summary_paths_under_root() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
review_root = Path(summary["review_root"]).resolve()

def check_path(label: str, path_value: str) -> None:
    path = Path(path_value).resolve()
    try:
        path.relative_to(review_root)
    except ValueError as exc:
        raise SystemExit(f"verification-summary {label} escapes review_root") from exc

for key, value in summary.items():
    if key == "review_root":
        continue
    if not key.endswith("_paths"):
        continue
    for path_key, path_value in value.items():
        check_path(f"{key}.{path_key}", path_value)

print("verification-summary paths under root: ok")
PY
}

assert_verification_summary_path_types() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))

def check_path(label: str, key: str, path_value: str) -> None:
    path = Path(path_value)
    if key.endswith("_dir"):
        if not path.is_dir():
            raise SystemExit(f"verification-summary {label} must be a directory")
    else:
        if not path.is_file():
            raise SystemExit(f"verification-summary {label} must be a file")

for block_name, value in summary.items():
    if block_name == "review_root":
        if not Path(value).is_dir():
            raise SystemExit("verification-summary review_root must be a directory")
        continue
    if not block_name.endswith("_paths"):
        continue
    for key, path_value in value.items():
        check_path(f"{block_name}.{key}", key, path_value)

print("verification-summary path types: ok")
PY
}

assert_verification_summary_unique_paths() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
seen = {}

def record(label: str, path_value: str) -> None:
    path = str(Path(path_value).resolve())
    prior = seen.get(path)
    if prior is not None:
        raise SystemExit(f"verification-summary duplicate path: {prior} and {label}")
    seen[path] = label

record("review_root", summary["review_root"])
for block_name, value in summary.items():
    if not block_name.endswith("_paths"):
        continue
    for key, path_value in value.items():
        record(f"{block_name}.{key}", path_value)

print("verification-summary unique paths: ok")
PY
}

assert_verification_summary_core_artifact_paths() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
core_paths = summary.get("core_artifact_paths", {})
required = {
    "manifest_json": root / "manifest.json",
    "semantic_truth_json": root / "semantic-truth.json",
}
for key, expected in required.items():
    actual = core_paths.get(key)
    if actual != str(expected.resolve()):
        raise SystemExit(f"verification-summary core_artifact_paths.{key} mismatch")
print("verification-summary core_artifact_paths: ok")
PY
}

assert_verification_summary_identification_audit_paths() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
audit_paths = summary.get("identification_audit_paths", {})
required = {
    "selfcheck_json": root / "identification-selfcheck.json",
    "eval_json": root / "identification-eval.json",
    "partials_json": root / "identification-partials.json",
    "challenges_json": root / "identification-challenges.json",
    "temporal_json": root / "identification-temporal.json",
}
for key, expected in required.items():
    actual = audit_paths.get(key)
    if actual != str(expected.resolve()):
        raise SystemExit(f"verification-summary identification_audit_paths.{key} mismatch")
print("verification-summary identification_audit_paths: ok")
PY
}

assert_verification_summary_identification_floor_paths() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
floor_paths = summary.get("identification_floor_paths", {})
required = {
    "regression_floors_json": root / "identification-regression-floors.json",
}
for key, expected in required.items():
    actual = floor_paths.get(key)
    if actual != str(expected.resolve()):
        raise SystemExit(f"verification-summary identification_floor_paths.{key} mismatch")
print("verification-summary identification_floor_paths: ok")
PY
}

assert_verification_summary_host_truth_paths() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
host_truth_paths = summary.get("host_truth_paths", {})
required = {
    "baseline_json": root / "host-truth-baseline.json",
    "compare_json": root / "host-truth-compare.json",
    "compare_html": root / "host-truth-compare.html",
}
for key, expected in required.items():
    actual = host_truth_paths.get(key)
    if actual != str(expected.resolve()):
        raise SystemExit(f"verification-summary host_truth_paths.{key} mismatch")
print("verification-summary host_truth_paths: ok")
PY
}

assert_verification_summary_expectation_paths() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
expectation_paths = summary.get("expectation_paths", {})
required = {
    "baseline_json": root / "expectations.json",
    "report_json": root / "expectation-report.json",
    "report_html": root / "expectation-report.html",
}
for key, expected in required.items():
    actual = expectation_paths.get(key)
    if actual != str(expected.resolve()):
        raise SystemExit(f"verification-summary expectation_paths.{key} mismatch")
print("verification-summary expectation_paths: ok")
PY
}

assert_verification_summary_repro_paths() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
repro_paths = summary.get("repro_paths", {})
required = {
    "compare_json": root / "repro-compare.json",
    "compare_html": root / "repro-compare.html",
}
for key, expected in required.items():
    actual = repro_paths.get(key)
    if actual != str(expected.resolve()):
        raise SystemExit(f"verification-summary repro_paths.{key} mismatch")
print("verification-summary repro_paths: ok")
PY
}

assert_verification_summary_capture_audit_paths() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
audit_paths = summary.get("capture_audit_paths", {})
required = {
    "image_report_json": root / "frame-image-regression-report.json",
    "meta_report_json": root / "frame-meta-regression-report.json",
    "semantic_report_json": root / "semantic-regression-report.json",
    "capture_report_json": root / "capture-regression-report.json",
}
for key, expected in required.items():
    actual = audit_paths.get(key)
    if actual != str(expected.resolve()):
        raise SystemExit(f"verification-summary capture_audit_paths.{key} mismatch")
print("verification-summary capture_audit_paths: ok")
PY
}

assert_verification_summary_regression_baseline_paths() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
baseline_paths = summary.get("regression_baseline_paths", {})
required = {
    "image_baseline_json": root / "frame-image-regression-baseline.json",
    "meta_baseline_json": root / "frame-meta-regression-baseline.json",
    "semantic_baseline_json": root / "semantic-regression-baseline.json",
}
for key, expected in required.items():
    actual = baseline_paths.get(key)
    if actual != str(expected.resolve()):
        raise SystemExit(f"verification-summary regression_baseline_paths.{key} mismatch")
print("verification-summary regression_baseline_paths: ok")
PY
}

assert_verification_summary_scene_root_paths() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
scene_root_paths = summary.get("scene_root_paths", {})
required = {
    "fishing_scene_dir": root / "fishing1",
    "mary_scene_dir": root / "mary1",
}
for key, expected in required.items():
    actual = scene_root_paths.get(key)
    if actual != str(expected.resolve()):
        raise SystemExit(f"verification-summary scene_root_paths.{key} mismatch")
print("verification-summary scene_root_paths: ok")
PY
}

assert_verification_summary_scene_asset_paths() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
asset_paths = summary.get("scene_asset_paths", {})
required = {
    "fishing_frames_dir": root / "fishing1" / "frames",
    "fishing_meta_dir": root / "fishing1" / "frame-meta",
    "mary_frames_dir": root / "mary1" / "frames",
    "mary_meta_dir": root / "mary1" / "frame-meta",
}
for key, expected in required.items():
    actual = asset_paths.get(key)
    if actual != str(expected.resolve()):
        raise SystemExit(f"verification-summary scene_asset_paths.{key} mismatch")
print("verification-summary scene_asset_paths: ok")
PY
}

assert_verification_summary_key_frame_paths() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
key_frame_paths = summary.get("key_frame_paths", {})
required = {
    "fishing_start_bmp": root / "fishing1" / "frames" / "frame_00000.bmp",
    "fishing_late_bmp": root / "fishing1" / "frames" / "frame_00080.bmp",
    "mary_start_bmp": root / "mary1" / "frames" / "frame_00000.bmp",
    "mary_late_bmp": root / "mary1" / "frames" / "frame_00100.bmp",
}
for key, expected in required.items():
    actual = key_frame_paths.get(key)
    if actual != str(expected.resolve()):
        raise SystemExit(f"verification-summary key_frame_paths.{key} mismatch")
print("verification-summary key_frame_paths: ok")
PY
}

assert_verification_summary_key_frame_meta_paths() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
key_frame_meta_paths = summary.get("key_frame_meta_paths", {})
required = {
    "fishing_start_json": root / "fishing1" / "frame-meta" / "frame_00000.json",
    "fishing_late_json": root / "fishing1" / "frame-meta" / "frame_00080.json",
    "mary_start_json": root / "mary1" / "frame-meta" / "frame_00000.json",
    "mary_late_json": root / "mary1" / "frame-meta" / "frame_00100.json",
}
for key, expected in required.items():
    actual = key_frame_meta_paths.get(key)
    if actual != str(expected.resolve()):
        raise SystemExit(f"verification-summary key_frame_meta_paths.{key} mismatch")
print("verification-summary key_frame_meta_paths: ok")
PY
}

assert_verification_summary_text_paths() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
review_paths = summary.get("review_paths", {})
summary_txt = (root / "verification-summary.txt").read_text(encoding="utf-8")
required_tokens = {
    f"review-root={summary['review_root']}",
    f"index={review_paths.get('index_html')}",
    f"identification={review_paths.get('identification_review_html')}",
    f"capture={review_paths.get('capture_regression_review_html')}",
    f"manifest-json={summary['core_artifact_paths']['manifest_json']}",
    f"semantic-truth-json={summary['core_artifact_paths']['semantic_truth_json']}",
    f"identify-selfcheck-json={summary['identification_audit_paths']['selfcheck_json']}",
    f"identify-eval-json={summary['identification_audit_paths']['eval_json']}",
    f"identify-partials-json={summary['identification_audit_paths']['partials_json']}",
    f"identify-challenges-json={summary['identification_audit_paths']['challenges_json']}",
    f"identify-temporal-json={summary['identification_audit_paths']['temporal_json']}",
    f"identify-regression-floors-json={summary['identification_floor_paths']['regression_floors_json']}",
    f"host-truth-baseline-json={summary['host_truth_paths']['baseline_json']}",
    f"host-truth-compare-json={summary['host_truth_paths']['compare_json']}",
    f"host-truth-compare-html={summary['host_truth_paths']['compare_html']}",
    f"expectations-json={summary['expectation_paths']['baseline_json']}",
    f"expectation-report-json={summary['expectation_paths']['report_json']}",
    f"expectation-report-html={summary['expectation_paths']['report_html']}",
    f"repro-compare-json={summary['repro_paths']['compare_json']}",
    f"repro-compare-html={summary['repro_paths']['compare_html']}",
    f"capture-image-report-json={summary['capture_audit_paths']['image_report_json']}",
    f"capture-meta-report-json={summary['capture_audit_paths']['meta_report_json']}",
    f"capture-semantic-report-json={summary['capture_audit_paths']['semantic_report_json']}",
    f"capture-report-json={summary['capture_audit_paths']['capture_report_json']}",
    f"image-baseline-json={summary['regression_baseline_paths']['image_baseline_json']}",
    f"meta-baseline-json={summary['regression_baseline_paths']['meta_baseline_json']}",
    f"semantic-baseline-json={summary['regression_baseline_paths']['semantic_baseline_json']}",
    f"fishing-scene-dir={summary['scene_root_paths']['fishing_scene_dir']}",
    f"mary-scene-dir={summary['scene_root_paths']['mary_scene_dir']}",
    f"fishing-frames-dir={summary['scene_asset_paths']['fishing_frames_dir']}",
    f"fishing-meta-dir={summary['scene_asset_paths']['fishing_meta_dir']}",
    f"mary-frames-dir={summary['scene_asset_paths']['mary_frames_dir']}",
    f"mary-meta-dir={summary['scene_asset_paths']['mary_meta_dir']}",
    f"fishing-start-bmp={summary['key_frame_paths']['fishing_start_bmp']}",
    f"fishing-late-bmp={summary['key_frame_paths']['fishing_late_bmp']}",
    f"mary-start-bmp={summary['key_frame_paths']['mary_start_bmp']}",
    f"mary-late-bmp={summary['key_frame_paths']['mary_late_bmp']}",
    f"fishing-start-json={summary['key_frame_meta_paths']['fishing_start_json']}",
    f"fishing-late-json={summary['key_frame_meta_paths']['fishing_late_json']}",
    f"mary-start-json={summary['key_frame_meta_paths']['mary_start_json']}",
    f"mary-late-json={summary['key_frame_meta_paths']['mary_late_json']}",
}
for token in required_tokens:
    if token not in summary_txt:
        raise SystemExit(f"verification-summary.txt missing token: {token}")
print("verification-summary txt paths: ok")
PY
}

assert_verification_summary_artifact_input_coverage() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
artifact_inputs = summary.get("artifact_inputs") or {}
for key, value in summary.items():
    if not key.endswith("_paths"):
        continue
    for path_key, path_value in value.items():
        if path_key.endswith("_dir"):
            continue
        rel = Path(path_value).resolve().relative_to(root).as_posix()
        if rel not in artifact_inputs:
            raise SystemExit(f"artifact_inputs missing summary file path: {key}.{path_key}")
print("verification-summary artifact input coverage: ok")
PY
}

assert_verification_summary_artifact_input_count() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
artifact_inputs = summary.get("artifact_inputs") or {}
if int(summary.get("artifact_input_count", -1)) != len(artifact_inputs):
    raise SystemExit("verification-summary artifact_input_count mismatch")
print("verification-summary artifact input count: ok")
PY
}

assert_verification_summary_path_entry_count() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))

count = 1 if summary.get("review_root") else 0
for key, value in summary.items():
    if key.endswith("_paths") and isinstance(value, dict):
        count += len(value)

if int(summary.get("path_entry_count", -1)) != count:
    raise SystemExit("verification-summary path_entry_count mismatch")
print("verification-summary path entry count: ok")
PY
}

assert_verification_summary_path_map_count() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))

count = sum(
    1
    for key, value in summary.items()
    if key.endswith("_paths") and isinstance(value, dict)
)

if int(summary.get("path_map_count", -1)) != count:
    raise SystemExit("verification-summary path_map_count mismatch")
print("verification-summary path map count: ok")
PY
}

assert_verification_summary_path_map_names() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
expected = sorted(
    key
    for key, value in summary.items()
    if key.endswith("_paths") and isinstance(value, dict)
)
actual = summary.get("path_map_names")
if actual != expected:
    raise SystemExit("verification-summary path_map_names mismatch")
print("verification-summary path map names: ok")
PY
}

assert_verification_summary_path_map_entry_counts() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
expected = {
    key: len(value)
    for key, value in sorted(summary.items())
    if key.endswith("_paths") and isinstance(value, dict)
}
actual = summary.get("path_map_entry_counts")
if actual != expected:
    raise SystemExit("verification-summary path_map_entry_counts mismatch")
print("verification-summary path map entry counts: ok")
PY
}

assert_verification_summary_path_map_type_counts() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
expected = {}
for key, value in sorted(summary.items()):
    if not key.endswith("_paths") or not isinstance(value, dict):
        continue
    file_count = 0
    dir_count = 0
    for path_key in value:
        if path_key.endswith("_dir"):
            dir_count += 1
        else:
            file_count += 1
    expected[key] = {"files": file_count, "dirs": dir_count}
actual = summary.get("path_map_type_counts")
if actual != expected:
    raise SystemExit("verification-summary path_map_type_counts mismatch")
print("verification-summary path map type counts: ok")
PY
}

assert_verification_summary_path_map_file_class_counts() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
expected = {}
for key, value in sorted(summary.items()):
    if not key.endswith("_paths") or not isinstance(value, dict):
        continue
    json_count = 0
    html_count = 0
    bmp_count = 0
    other_count = 0
    for path_key, path_value in value.items():
        if path_key.endswith("_dir"):
            continue
        suffix = Path(path_value).suffix.lower()
        if suffix == ".json":
            json_count += 1
        elif suffix == ".html":
            html_count += 1
        elif suffix == ".bmp":
            bmp_count += 1
        else:
            other_count += 1
    expected[key] = {
        "json": json_count,
        "html": html_count,
        "bmp": bmp_count,
        "other": other_count,
    }
actual = summary.get("path_map_file_class_counts")
if actual != expected:
    raise SystemExit("verification-summary path_map_file_class_counts mismatch")
print("verification-summary path map file class counts: ok")
PY
}

assert_verification_summary_path_map_entry_names() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
expected = {
    key: sorted(value.keys())
    for key, value in sorted(summary.items())
    if key.endswith("_paths") and isinstance(value, dict)
}
actual = summary.get("path_map_entry_names")
if actual != expected:
    raise SystemExit("verification-summary path_map_entry_names mismatch")
print("verification-summary path map entry names: ok")
PY
}

assert_verification_summary_path_basenames() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
expected = []
if summary.get("review_root"):
    expected.append(Path(summary["review_root"]).name)
for key, value in sorted(summary.items()):
    if not key.endswith("_paths") or not isinstance(value, dict):
        continue
    for path_value in value.values():
        expected.append(Path(path_value).name)
expected = sorted(expected)
actual = summary.get("path_basenames")
if actual != expected:
    raise SystemExit("verification-summary path_basenames mismatch")
print("verification-summary path basenames: ok")
PY
}

assert_verification_summary_path_relpaths() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
review_root = Path(summary["review_root"]).resolve()
expected = ["."]
for key, value in sorted(summary.items()):
    if not key.endswith("_paths") or not isinstance(value, dict):
        continue
    for path_value in value.values():
        expected.append(Path(path_value).resolve().relative_to(review_root).as_posix())
expected = sorted(expected)
actual = summary.get("path_relpaths")
if actual != expected:
    raise SystemExit("verification-summary path_relpaths mismatch")
print("verification-summary path relpaths: ok")
PY
}

assert_verification_summary_path_depth_counts() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
counts = {}
for relpath in summary.get("path_relpaths", []):
    depth = 0 if relpath == "." else relpath.count("/") + 1
    counts[str(depth)] = counts.get(str(depth), 0) + 1
counts = dict(sorted(counts.items(), key=lambda item: int(item[0])))
if summary.get("path_depth_counts") != counts:
    raise SystemExit("verification-summary path_depth_counts mismatch")
print("verification-summary path depth counts: ok")
PY
}

assert_verification_summary_path_max_depth() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
actual = max(
    0 if relpath == "." else relpath.count("/") + 1
    for relpath in summary.get("path_relpaths", [])
)
if int(summary.get("path_max_depth", -1)) != actual:
    raise SystemExit("verification-summary path_max_depth mismatch")
print("verification-summary path max depth: ok")
PY
}

assert_verification_summary_path_min_nonroot_depth() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
nonroot_depths = [
    relpath.count("/") + 1
    for relpath in summary.get("path_relpaths", [])
    if relpath != "."
]
actual = min(nonroot_depths) if nonroot_depths else 0
if int(summary.get("path_min_nonroot_depth", -1)) != actual:
    raise SystemExit("verification-summary path_min_nonroot_depth mismatch")
print("verification-summary path min nonroot depth: ok")
PY
}

assert_verification_summary_path_type_counts() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))

file_count = 0
dir_count = 1 if summary.get("review_root") else 0
for key, value in summary.items():
    if not key.endswith("_paths") or not isinstance(value, dict):
        continue
    for path_key in value:
        if path_key.endswith("_dir"):
            dir_count += 1
        else:
            file_count += 1

if int(summary.get("path_file_count", -1)) != file_count:
    raise SystemExit("verification-summary path_file_count mismatch")
if int(summary.get("path_dir_count", -1)) != dir_count:
    raise SystemExit("verification-summary path_dir_count mismatch")
print("verification-summary path type counts: ok")
PY
}

assert_verification_summary_path_class_counts() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))

json_count = 0
html_count = 0
bmp_count = 0
other_file_count = 0
for key, value in summary.items():
    if not key.endswith("_paths") or not isinstance(value, dict):
        continue
    for path_key, path_value in value.items():
        if path_key.endswith("_dir"):
            continue
        suffix = Path(path_value).suffix.lower()
        if suffix == ".json":
            json_count += 1
        elif suffix == ".html":
            html_count += 1
        elif suffix == ".bmp":
            bmp_count += 1
        else:
            other_file_count += 1

if int(summary.get("path_json_count", -1)) != json_count:
    raise SystemExit("verification-summary path_json_count mismatch")
if int(summary.get("path_html_count", -1)) != html_count:
    raise SystemExit("verification-summary path_html_count mismatch")
if int(summary.get("path_bmp_count", -1)) != bmp_count:
    raise SystemExit("verification-summary path_bmp_count mismatch")
if int(summary.get("path_other_file_count", -1)) != other_file_count:
    raise SystemExit("verification-summary path_other_file_count mismatch")
print("verification-summary path class counts: ok")
PY
}

print_review_paths() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
summary = json.loads((root / "verification-summary.json").read_text(encoding="utf-8"))
review_paths = summary.get("review_paths", {})
print(
    "review-paths: ok "
    f"index={review_paths.get('index_html')} "
    f"identification={review_paths.get('identification_review_html')} "
    f"capture={review_paths.get('capture_regression_review_html')}"
)
PY
}

assert_manifest_dashboard_links() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
extras = manifest.get("extras", {})
required = {
    "identification-review.html": root / "identification-review.html",
    "capture-regression-review.html": root / "capture-regression-review.html",
    "capture-regression-report.json": root / "capture-regression-report.json",
    "verification-summary.json": root / "verification-summary.json",
    "verification-summary.txt": root / "verification-summary.txt",
    "semantic-truth.json": root / "semantic-truth.json",
}
for key, expected in required.items():
    actual = extras.get(key)
    if actual != str(expected.resolve()):
        raise SystemExit(f"manifest extras.{key} mismatch")
print("manifest dashboard links: ok")
PY
}

assert_dashboard_html_links() {
    local root="$1"
    python3 - "$root" <<'PY'
import sys
from pathlib import Path

root = Path(sys.argv[1])
index_html = (root / "index.html").read_text(encoding="utf-8")
capture_html = (root / "capture-regression-review.html").read_text(encoding="utf-8")

required_index_links = [
    "identification-review.html",
    "capture-regression-review.html",
    "capture-regression-report.json",
    "verification-summary.json",
    "verification-summary.txt",
    "semantic-truth.json",
]
for href in required_index_links:
    if href not in index_html:
        raise SystemExit(f"index.html missing link: {href}")

required_capture_links = [
    "index.html",
    "identification-review.html",
    "verification-summary.json",
    "verification-summary.txt",
    "semantic-truth.json",
    "frame-image-regression-report.json",
    "frame-meta-regression-report.json",
    "semantic-regression-report.json",
    "capture-regression-report.json",
    "fishing1/frames/",
    "mary1/frames/",
    "fishing1/frame-meta/",
    "mary1/frame-meta/",
]
for href in required_capture_links:
    if href not in capture_html:
        raise SystemExit(f"capture-regression-review.html missing link: {href}")

print("dashboard html links: ok")
PY
}

assert_identification_review_links() {
    local root="$1"
    python3 - "$root" <<'PY'
import sys
from pathlib import Path

root = Path(sys.argv[1])
html = (root / "identification-review.html").read_text(encoding="utf-8")
required = [
    "index.html",
    "capture-regression-review.html",
    "capture-regression-report.json",
    "verification-summary.json",
    "verification-summary.txt",
    "frame-image-regression-report.json",
    "frame-meta-regression-report.json",
    "semantic-regression-report.json",
    "identification-selfcheck.json",
    "identification-eval.json",
    "identification-partials.json",
    "identification-challenges.json",
    "identification-temporal.json",
    "semantic-truth.json",
    "fishing1/frames/frame_00000.bmp",
    "mary1/frames/frame_00000.bmp",
    "fishing1/frames/frame_00080.bmp",
    "mary1/frames/frame_00100.bmp",
]
for href in required:
    if href not in html:
        raise SystemExit(f"identification-review.html missing link: {href}")
print("identification-review html links: ok")
PY
}

assert_capture_review_totals() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
report = json.loads((root / "capture-regression-report.json").read_text(encoding="utf-8"))
html = (root / "capture-regression-review.html").read_text(encoding="utf-8")
totals = report.get("totals", {})
required = [
    ("Frame Image Failures", totals.get("frame_image_failures", 0)),
    ("Frame Meta Failures", totals.get("frame_meta_failures", 0)),
    ("Semantic Failures", totals.get("semantic_failures", 0)),
]
for label, value in required:
    pattern = rf"{re.escape(label)}</div>\s*<div class=\"value [^\"]+\">{re.escape(str(value))}</div>"
    if not re.search(pattern, html):
        raise SystemExit(f"capture-regression-review.html totals mismatch: {label}={value}")
print("capture-regression review totals: ok")
PY
}

assert_capture_review_tightest_drift() {
    local root="$1"
    python3 - "$root" <<'PY'
import json
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
report = json.loads((root / "capture-regression-report.json").read_text(encoding="utf-8"))
html = (root / "capture-regression-review.html").read_text(encoding="utf-8")
tightest = report.get("tightest_drift")
if not tightest:
    print("capture-regression review tightest drift: ok")
    raise SystemExit(0)

scene = tightest.get("scene") or tightest.get("scene_label") or ""
field = tightest.get("field") or ""
check = tightest.get("check") or ""
summary_pattern = rf"Tightest Drift</h2>\s*<div>{re.escape(str(check))} / {re.escape(str(scene))} / {re.escape(str(field))}"
if not re.search(summary_pattern, html):
    raise SystemExit("capture-regression-review.html tightest drift summary mismatch")

frame = tightest.get("frame")
if frame:
    scene_slug = str(scene).lower().replace(" ", "")
    for href in (
        f'{scene_slug}/frames/{frame}.bmp',
        f'{scene_slug}/frame-meta/{frame}.json',
    ):
        if href not in html:
            raise SystemExit(f"capture-regression-review.html tightest drift asset missing: {href}")

print("capture-regression review tightest drift: ok")
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


def severity(value):
    if value is None:
        return "neutral"
    try:
        numeric = float(value)
    except (TypeError, ValueError):
        return "neutral"
    if numeric <= 1.0:
        return "danger"
    if numeric <= 3.0:
        return "warn"
    return "safe"


def count_path_entries(summary_obj):
    total = 1 if summary_obj.get("review_root") else 0
    for key, value in summary_obj.items():
        if key.endswith("_paths") and isinstance(value, dict):
            total += len(value)
    return total


def count_path_maps(summary_obj):
    return sum(
        1
        for key, value in summary_obj.items()
        if key.endswith("_paths") and isinstance(value, dict)
    )


def list_path_map_names(summary_obj):
    return sorted(
        key
        for key, value in summary_obj.items()
        if key.endswith("_paths") and isinstance(value, dict)
    )


def path_map_entry_counts(summary_obj):
    return {
        key: len(value)
        for key, value in sorted(summary_obj.items())
        if key.endswith("_paths") and isinstance(value, dict)
    }


def path_map_type_counts(summary_obj):
    counts = {}
    for key, value in sorted(summary_obj.items()):
        if not key.endswith("_paths") or not isinstance(value, dict):
            continue
        file_count = 0
        dir_count = 0
        for path_key in value:
            if path_key.endswith("_dir"):
                dir_count += 1
            else:
                file_count += 1
        counts[key] = {"files": file_count, "dirs": dir_count}
    return counts


def path_map_file_class_counts(summary_obj):
    counts = {}
    for key, value in sorted(summary_obj.items()):
        if not key.endswith("_paths") or not isinstance(value, dict):
            continue
        json_count = 0
        html_count = 0
        bmp_count = 0
        other_count = 0
        for path_key, path_value in value.items():
            if path_key.endswith("_dir"):
                continue
            suffix = Path(path_value).suffix.lower()
            if suffix == ".json":
                json_count += 1
            elif suffix == ".html":
                html_count += 1
            elif suffix == ".bmp":
                bmp_count += 1
            else:
                other_count += 1
        counts[key] = {
            "json": json_count,
            "html": html_count,
            "bmp": bmp_count,
            "other": other_count,
        }
    return counts


def path_map_entry_names(summary_obj):
    return {
        key: sorted(value.keys())
        for key, value in sorted(summary_obj.items())
        if key.endswith("_paths") and isinstance(value, dict)
    }


def path_basenames(summary_obj):
    basenames = []
    if summary_obj.get("review_root"):
        basenames.append(Path(summary_obj["review_root"]).name)
    for key, value in sorted(summary_obj.items()):
        if not key.endswith("_paths") or not isinstance(value, dict):
            continue
        for path_value in value.values():
            basenames.append(Path(path_value).name)
    return sorted(basenames)


def path_relpaths(summary_obj):
    review_root = Path(summary_obj["review_root"]).resolve()
    relpaths = ["."]
    for key, value in sorted(summary_obj.items()):
        if not key.endswith("_paths") or not isinstance(value, dict):
            continue
        for path_value in value.values():
            relpaths.append(Path(path_value).resolve().relative_to(review_root).as_posix())
    return sorted(relpaths)


def path_depth_counts(summary_obj):
    counts = {}
    for relpath in path_relpaths(summary_obj):
        depth = 0 if relpath == "." else relpath.count("/") + 1
        counts[str(depth)] = counts.get(str(depth), 0) + 1
    return dict(sorted(counts.items(), key=lambda item: int(item[0])))


def max_path_depth(summary_obj):
    return max(
        0 if relpath == "." else relpath.count("/") + 1
        for relpath in path_relpaths(summary_obj)
    )


def min_nonroot_path_depth(summary_obj):
    nonroot_depths = [
        relpath.count("/") + 1
        for relpath in path_relpaths(summary_obj)
        if relpath != "."
    ]
    return min(nonroot_depths) if nonroot_depths else 0


def count_path_types(summary_obj):
    file_count = 0
    dir_count = 1 if summary_obj.get("review_root") else 0
    for key, value in summary_obj.items():
        if not key.endswith("_paths") or not isinstance(value, dict):
            continue
        for path_key in value:
            if path_key.endswith("_dir"):
                dir_count += 1
            else:
                file_count += 1
    return file_count, dir_count


def count_path_classes(summary_obj):
    json_count = 0
    html_count = 0
    bmp_count = 0
    dir_count = 1 if summary_obj.get("review_root") else 0
    other_file_count = 0
    for key, value in summary_obj.items():
        if not key.endswith("_paths") or not isinstance(value, dict):
            continue
        for path_key, path_value in value.items():
            if path_key.endswith("_dir"):
                dir_count += 1
                continue
            suffix = Path(path_value).suffix.lower()
            if suffix == ".json":
                json_count += 1
            elif suffix == ".html":
                html_count += 1
            elif suffix == ".bmp":
                bmp_count += 1
            else:
                other_file_count += 1
    return json_count, html_count, bmp_count, dir_count, other_file_count

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

identify_challenges_path = root / "identification-challenges.json"
identify_challenges = {
    "present": identify_challenges_path.is_file(),
    "passed": False,
    "query_count": 0,
    "unknown_score_limit": None,
    "unknown_margin_limit": None,
    "ambiguous_score_limit": None,
    "ambiguous_margin_limit": None,
    "max_best_score": None,
    "max_margin": None,
    "max_unknown_best_score": None,
    "max_unknown_margin": None,
    "max_ambiguous_best_score": None,
    "max_ambiguous_margin": None,
    "unknown_score_headroom": None,
    "unknown_margin_headroom": None,
    "ambiguous_score_headroom": None,
    "ambiguous_margin_headroom": None,
    "tightest_query_label": None,
    "tightest_status": None,
    "tightest_best_scene_label": None,
    "tightest_metric": None,
    "tightest_value": None,
    "tightest_limit": None,
    "tightest_headroom": None,
    "tightest_pressure": None,
    "warn_count": 0,
    "danger_count": 0,
    "ambiguous_count": 0,
    "unknown_count": 0,
}
if identify_challenges_path.is_file():
    payload = json.loads(identify_challenges_path.read_text(encoding="utf-8"))
    identify_challenges["passed"] = bool(payload.get("passed"))
    identify_challenges["query_count"] = int(payload.get("query_count", 0))
    identify_challenges["unknown_score_limit"] = payload.get("unknown_score_limit")
    identify_challenges["unknown_margin_limit"] = payload.get("unknown_margin_limit")
    identify_challenges["ambiguous_score_limit"] = payload.get("ambiguous_score_limit")
    identify_challenges["ambiguous_margin_limit"] = payload.get("ambiguous_margin_limit")
    identify_challenges["max_best_score"] = payload.get("max_best_score")
    identify_challenges["max_margin"] = payload.get("max_margin")
    identify_challenges["max_unknown_best_score"] = payload.get("max_unknown_best_score")
    identify_challenges["max_unknown_margin"] = payload.get("max_unknown_margin")
    identify_challenges["max_ambiguous_best_score"] = payload.get("max_ambiguous_best_score")
    identify_challenges["max_ambiguous_margin"] = payload.get("max_ambiguous_margin")
    identify_challenges["unknown_score_headroom"] = payload.get("unknown_score_headroom")
    identify_challenges["unknown_margin_headroom"] = payload.get("unknown_margin_headroom")
    identify_challenges["ambiguous_score_headroom"] = payload.get("ambiguous_score_headroom")
    identify_challenges["ambiguous_margin_headroom"] = payload.get("ambiguous_margin_headroom")
    identify_challenges["tightest_query_label"] = payload.get("tightest_query_label")
    identify_challenges["tightest_status"] = payload.get("tightest_status")
    identify_challenges["tightest_best_scene_label"] = payload.get("tightest_best_scene_label")
    identify_challenges["tightest_metric"] = payload.get("tightest_metric")
    identify_challenges["tightest_value"] = payload.get("tightest_value")
    identify_challenges["tightest_limit"] = payload.get("tightest_limit")
    identify_challenges["tightest_headroom"] = payload.get("tightest_headroom")
    identify_challenges["tightest_pressure"] = payload.get("tightest_pressure")
    levels = [
        severity(identify_challenges["unknown_score_headroom"]),
        severity(identify_challenges["unknown_margin_headroom"]),
        severity(identify_challenges["ambiguous_score_headroom"]),
        severity(identify_challenges["ambiguous_margin_headroom"]),
    ]
    identify_challenges["warn_count"] = sum(1 for level in levels if level == "warn")
    identify_challenges["danger_count"] = sum(1 for level in levels if level == "danger")
    identify_challenges["ambiguous_count"] = int(payload.get("ambiguous_count", 0))
    identify_challenges["unknown_count"] = int(payload.get("unknown_count", 0))
checks["identification-challenges"] = identify_challenges

identify_temporal_path = root / "identification-temporal.json"
identify_temporal = {
    "present": identify_temporal_path.is_file(),
    "passed": False,
    "query_count": 0,
    "min_identified_margin": None,
    "min_identified_ratio": None,
    "max_score_drop": None,
    "max_identified_margin_drop": None,
}
if identify_temporal_path.is_file():
    payload = json.loads(identify_temporal_path.read_text(encoding="utf-8"))
    identify_temporal["passed"] = bool(payload.get("passed"))
    identify_temporal["query_count"] = int(payload.get("query_count", 0))
    identify_temporal["min_identified_margin"] = payload.get("min_identified_margin")
    identify_temporal["min_identified_ratio"] = payload.get("min_identified_ratio")
    identify_temporal["max_score_drop"] = payload.get("max_score_drop")
    identify_temporal["max_identified_margin_drop"] = payload.get("max_identified_margin_drop")
checks["identification-temporal"] = identify_temporal

capture_regression_path = root / "capture-regression-report.json"
capture_regression = {
    "present": capture_regression_path.is_file(),
    "passed": False,
    "frame_image_passed": False,
    "frame_image_failure_count": None,
    "frame_image_first_failed_scene": None,
    "frame_meta_passed": False,
    "frame_meta_failure_count": None,
    "frame_meta_first_failed_scene": None,
    "semantic_passed": False,
    "semantic_failure_count": None,
    "semantic_first_failed_scene": None,
    "total_failure_count": None,
}
if capture_regression_path.is_file():
    payload = json.loads(capture_regression_path.read_text(encoding="utf-8"))
    checks_payload = payload.get("checks", {})
    frame_image = checks_payload.get("frame-image", {})
    frame_meta = checks_payload.get("frame-meta", {})
    semantic = checks_payload.get("semantic", {})
    capture_regression["passed"] = bool(payload.get("passed"))
    capture_regression["frame_image_passed"] = bool(frame_image.get("passed"))
    capture_regression["frame_image_failure_count"] = frame_image.get("failure_count")
    capture_regression["frame_image_first_failed_scene"] = (payload.get("first_failed_scenes") or {}).get("frame-image")
    capture_regression["frame_meta_passed"] = bool(frame_meta.get("passed"))
    capture_regression["frame_meta_failure_count"] = frame_meta.get("failure_count")
    capture_regression["frame_meta_first_failed_scene"] = (payload.get("first_failed_scenes") or {}).get("frame-meta")
    capture_regression["semantic_passed"] = bool(semantic.get("passed"))
    capture_regression["semantic_failure_count"] = semantic.get("failure_count")
    capture_regression["semantic_first_failed_scene"] = (payload.get("first_failed_scenes") or {}).get("semantic")
    capture_regression["total_failure_count"] = (
        int(frame_image.get("failure_count", 0))
        + int(frame_meta.get("failure_count", 0))
        + int(semantic.get("failure_count", 0))
    )
checks["capture-regression"] = capture_regression

review_paths = {
    "index_html": str((root / "index.html").resolve()),
    "identification_review_html": str((root / "identification-review.html").resolve()),
    "capture_regression_review_html": str((root / "capture-regression-review.html").resolve()),
}
core_artifact_paths = {
    "manifest_json": str((root / "manifest.json").resolve()),
    "semantic_truth_json": str((root / "semantic-truth.json").resolve()),
}
identification_audit_paths = {
    "selfcheck_json": str((root / "identification-selfcheck.json").resolve()),
    "eval_json": str((root / "identification-eval.json").resolve()),
    "partials_json": str((root / "identification-partials.json").resolve()),
    "challenges_json": str((root / "identification-challenges.json").resolve()),
    "temporal_json": str((root / "identification-temporal.json").resolve()),
}
identification_floor_paths = {
    "regression_floors_json": str((root / "identification-regression-floors.json").resolve()),
}
host_truth_paths = {
    "baseline_json": str((root / "host-truth-baseline.json").resolve()),
    "compare_json": str((root / "host-truth-compare.json").resolve()),
    "compare_html": str((root / "host-truth-compare.html").resolve()),
}
expectation_paths = {
    "baseline_json": str((root / "expectations.json").resolve()),
    "report_json": str((root / "expectation-report.json").resolve()),
    "report_html": str((root / "expectation-report.html").resolve()),
}
repro_paths = {
    "compare_json": str((root / "repro-compare.json").resolve()),
    "compare_html": str((root / "repro-compare.html").resolve()),
}
capture_audit_paths = {
    "image_report_json": str((root / "frame-image-regression-report.json").resolve()),
    "meta_report_json": str((root / "frame-meta-regression-report.json").resolve()),
    "semantic_report_json": str((root / "semantic-regression-report.json").resolve()),
    "capture_report_json": str((root / "capture-regression-report.json").resolve()),
}
regression_baseline_paths = {
    "image_baseline_json": str((root / "frame-image-regression-baseline.json").resolve()),
    "meta_baseline_json": str((root / "frame-meta-regression-baseline.json").resolve()),
    "semantic_baseline_json": str((root / "semantic-regression-baseline.json").resolve()),
}
scene_root_paths = {
    "fishing_scene_dir": str((root / "fishing1").resolve()),
    "mary_scene_dir": str((root / "mary1").resolve()),
}
scene_asset_paths = {
    "fishing_frames_dir": str((root / "fishing1" / "frames").resolve()),
    "fishing_meta_dir": str((root / "fishing1" / "frame-meta").resolve()),
    "mary_frames_dir": str((root / "mary1" / "frames").resolve()),
    "mary_meta_dir": str((root / "mary1" / "frame-meta").resolve()),
}
key_frame_paths = {
    "fishing_start_bmp": str((root / "fishing1" / "frames" / "frame_00000.bmp").resolve()),
    "fishing_late_bmp": str((root / "fishing1" / "frames" / "frame_00080.bmp").resolve()),
    "mary_start_bmp": str((root / "mary1" / "frames" / "frame_00000.bmp").resolve()),
    "mary_late_bmp": str((root / "mary1" / "frames" / "frame_00100.bmp").resolve()),
}
key_frame_meta_paths = {
    "fishing_start_json": str((root / "fishing1" / "frame-meta" / "frame_00000.json").resolve()),
    "fishing_late_json": str((root / "fishing1" / "frame-meta" / "frame_00080.json").resolve()),
    "mary_start_json": str((root / "mary1" / "frame-meta" / "frame_00000.json").resolve()),
    "mary_late_json": str((root / "mary1" / "frame-meta" / "frame_00100.json").resolve()),
}

digest_inputs = {}
for path_map in (
    review_paths,
    core_artifact_paths,
    identification_audit_paths,
    identification_floor_paths,
    host_truth_paths,
    expectation_paths,
    repro_paths,
    capture_audit_paths,
    regression_baseline_paths,
    key_frame_paths,
    key_frame_meta_paths,
):
    for key, path_value in path_map.items():
        if key.endswith("_dir"):
            continue
        path = Path(path_value)
        if path.is_file():
            digest_inputs[path.relative_to(root).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()

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
    "review_root": str(root.resolve()),
    "review_paths": review_paths,
    "core_artifact_paths": core_artifact_paths,
    "identification_audit_paths": identification_audit_paths,
    "identification_floor_paths": identification_floor_paths,
    "host_truth_paths": host_truth_paths,
    "expectation_paths": expectation_paths,
    "repro_paths": repro_paths,
    "capture_audit_paths": capture_audit_paths,
    "regression_baseline_paths": regression_baseline_paths,
    "scene_root_paths": scene_root_paths,
    "scene_asset_paths": scene_asset_paths,
    "key_frame_paths": key_frame_paths,
    "key_frame_meta_paths": key_frame_meta_paths,
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
        and checks["identification-challenges"]["present"]
        and checks["identification-challenges"]["passed"]
        and checks["identification-temporal"]["present"]
        and checks["identification-temporal"]["passed"]
        and checks["capture-regression"]["present"]
        and checks["capture-regression"]["passed"]
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
summary["path_entry_count"] = count_path_entries(summary)
summary["path_map_count"] = count_path_maps(summary)
summary["path_map_names"] = list_path_map_names(summary)
summary["path_map_entry_counts"] = path_map_entry_counts(summary)
summary["path_map_type_counts"] = path_map_type_counts(summary)
summary["path_map_file_class_counts"] = path_map_file_class_counts(summary)
summary["path_map_entry_names"] = path_map_entry_names(summary)
summary["path_basenames"] = path_basenames(summary)
summary["path_relpaths"] = path_relpaths(summary)
summary["path_depth_counts"] = path_depth_counts(summary)
summary["path_max_depth"] = max_path_depth(summary)
summary["path_min_nonroot_depth"] = min_nonroot_path_depth(summary)
summary["path_file_count"], summary["path_dir_count"] = count_path_types(summary)
summary["artifact_input_count"] = len(digest_inputs)
(
    summary["path_json_count"],
    summary["path_html_count"],
    summary["path_bmp_count"],
    _summary_dir_count,
    summary["path_other_file_count"],
) = count_path_classes(summary)
summary["risk_status"] = (
    "elevated_risk"
    if checks["identification-challenges"]["danger_count"] > 0
    else (
        "warning"
        if checks["identification-challenges"]["warn_count"] > 0
        else "normal"
    )
)

(root / "verification-summary.json").write_text(
    json.dumps(summary, indent=2) + "\n",
    encoding="utf-8",
)

(root / "verification-summary.txt").write_text(
    "status={status} risk={risk_status} git={git_head_short} digest={digest} "
    "identify-selfcheck={identify_selfcheck} identify-eval={identify_eval} identify-partials={identify_partials} identify-challenges={identify_challenges} identify-temporal={identify_temporal} "
    "capture-regression={capture_regression} capture-failures={capture_failures} "
    "capture-first-image={capture_first_image} capture-first-meta={capture_first_meta} capture-first-semantic={capture_first_semantic} "
    "review-root={review_root} path-map-count={path_map_count} path-map-names={path_map_names} path-map-entry-counts={path_map_entry_counts} path-map-type-counts={path_map_type_counts} path-map-file-class-counts={path_map_file_class_counts} path-map-entry-names={path_map_entry_names} path-basenames={path_basenames} path-relpaths={path_relpaths} path-depth-counts={path_depth_counts} path-max-depth={path_max_depth} path-min-nonroot-depth={path_min_nonroot_depth} path-entry-count={path_entry_count} path-file-count={path_file_count} path-dir-count={path_dir_count} artifact-input-count={artifact_input_count} "
    "path-json-count={path_json_count} path-html-count={path_html_count} path-bmp-count={path_bmp_count} path-other-file-count={path_other_file_count} "
    "index={index_html} identification={identification_html} capture={capture_html} "
    "manifest-json={manifest_json} semantic-truth-json={semantic_truth_json} "
    "identify-selfcheck-json={identify_selfcheck_json} identify-eval-json={identify_eval_json} "
    "identify-partials-json={identify_partials_json} identify-challenges-json={identify_challenges_json} "
    "identify-temporal-json={identify_temporal_json} "
    "identify-regression-floors-json={identify_regression_floors_json} "
    "host-truth-baseline-json={host_truth_baseline_json} host-truth-compare-json={host_truth_compare_json} host-truth-compare-html={host_truth_compare_html} "
    "expectations-json={expectations_json} expectation-report-json={expectation_report_json} expectation-report-html={expectation_report_html} "
    "repro-compare-json={repro_compare_json} repro-compare-html={repro_compare_html} "
    "capture-image-report-json={capture_image_report_json} capture-meta-report-json={capture_meta_report_json} "
    "capture-semantic-report-json={capture_semantic_report_json} capture-report-json={capture_report_json} "
    "image-baseline-json={image_baseline_json} meta-baseline-json={meta_baseline_json} "
    "semantic-baseline-json={semantic_baseline_json} "
    "fishing-scene-dir={fishing_scene_dir} mary-scene-dir={mary_scene_dir} "
    "fishing-frames-dir={fishing_frames_dir} fishing-meta-dir={fishing_meta_dir} "
    "mary-frames-dir={mary_frames_dir} mary-meta-dir={mary_meta_dir} "
    "fishing-start-bmp={fishing_start_bmp} fishing-late-bmp={fishing_late_bmp} "
    "mary-start-bmp={mary_start_bmp} mary-late-bmp={mary_late_bmp} "
    "fishing-start-json={fishing_start_json} fishing-late-json={fishing_late_json} "
    "mary-start-json={mary_start_json} mary-late-json={mary_late_json} "
    "identify-ratio={identify_ratio} "
    "challenge-unknown-score={challenge_unknown_score} challenge-unknown-margin={challenge_unknown_margin} "
    "challenge-ambiguous-score={challenge_ambiguous_score} challenge-ambiguous-margin={challenge_ambiguous_margin} "
    "challenge-unknown-headroom={challenge_unknown_headroom} challenge-unknown-margin-headroom={challenge_unknown_margin_headroom} "
    "challenge-ambiguous-headroom={challenge_ambiguous_headroom} challenge-ambiguous-margin-headroom={challenge_ambiguous_margin_headroom} "
    "challenge-warn-count={challenge_warn_count} challenge-danger-count={challenge_danger_count} "
    "challenge-tightest={challenge_tightest} challenge-tightest-metric={challenge_tightest_metric} "
    "challenge-tightest-headroom={challenge_tightest_headroom} challenge-tightest-pressure={challenge_tightest_pressure} "
    "expectation-report={expectation} host-truth-compare={host_truth} repro-compare={repro}\n".format(
        status="PASS" if summary["all_passed"] else "FAIL",
        risk_status=summary["risk_status"],
        git_head_short=git_head_short,
        digest=summary["artifact_sha256"],
        identify_selfcheck="ok" if checks["identification-selfcheck"]["passed"] else "fail",
        identify_eval="ok" if checks["identification-eval"]["passed"] else "fail",
        identify_partials="ok" if checks["identification-partials"]["passed"] else "fail",
        identify_challenges="ok" if checks["identification-challenges"]["passed"] else "fail",
        identify_temporal="ok" if checks["identification-temporal"]["passed"] else "fail",
        capture_regression="ok" if checks["capture-regression"]["passed"] else "fail",
        capture_failures=checks["capture-regression"]["total_failure_count"],
        capture_first_image=checks["capture-regression"]["frame_image_first_failed_scene"],
        capture_first_meta=checks["capture-regression"]["frame_meta_first_failed_scene"],
        capture_first_semantic=checks["capture-regression"]["semantic_first_failed_scene"],
        review_root=summary["review_root"],
        path_map_count=summary["path_map_count"],
        path_map_names=",".join(summary["path_map_names"]),
        path_map_entry_counts=",".join(
            f"{key}:{summary['path_map_entry_counts'][key]}"
            for key in sorted(summary["path_map_entry_counts"])
        ),
        path_map_type_counts=",".join(
            f"{key}:{summary['path_map_type_counts'][key]['files']}f/{summary['path_map_type_counts'][key]['dirs']}d"
            for key in sorted(summary["path_map_type_counts"])
        ),
        path_map_file_class_counts=",".join(
            f"{key}:{summary['path_map_file_class_counts'][key]['json']}j/{summary['path_map_file_class_counts'][key]['html']}h/{summary['path_map_file_class_counts'][key]['bmp']}b/{summary['path_map_file_class_counts'][key]['other']}o"
            for key in sorted(summary["path_map_file_class_counts"])
        ),
        path_map_entry_names=",".join(
            f"{key}:{'|'.join(summary['path_map_entry_names'][key])}"
            for key in sorted(summary["path_map_entry_names"])
        ),
        path_basenames=",".join(summary["path_basenames"]),
        path_relpaths=",".join(summary["path_relpaths"]),
        path_depth_counts=",".join(
            f"{depth}:{summary['path_depth_counts'][depth]}"
            for depth in sorted(summary["path_depth_counts"], key=int)
        ),
        path_max_depth=summary["path_max_depth"],
        path_min_nonroot_depth=summary["path_min_nonroot_depth"],
        path_entry_count=summary["path_entry_count"],
        path_file_count=summary["path_file_count"],
        path_dir_count=summary["path_dir_count"],
        artifact_input_count=summary["artifact_input_count"],
        path_json_count=summary["path_json_count"],
        path_html_count=summary["path_html_count"],
        path_bmp_count=summary["path_bmp_count"],
        path_other_file_count=summary["path_other_file_count"],
        index_html=summary["review_paths"]["index_html"],
        identification_html=summary["review_paths"]["identification_review_html"],
        capture_html=summary["review_paths"]["capture_regression_review_html"],
        manifest_json=summary["core_artifact_paths"]["manifest_json"],
        semantic_truth_json=summary["core_artifact_paths"]["semantic_truth_json"],
        identify_selfcheck_json=summary["identification_audit_paths"]["selfcheck_json"],
        identify_eval_json=summary["identification_audit_paths"]["eval_json"],
        identify_partials_json=summary["identification_audit_paths"]["partials_json"],
        identify_challenges_json=summary["identification_audit_paths"]["challenges_json"],
        identify_temporal_json=summary["identification_audit_paths"]["temporal_json"],
        identify_regression_floors_json=summary["identification_floor_paths"]["regression_floors_json"],
        host_truth_baseline_json=summary["host_truth_paths"]["baseline_json"],
        host_truth_compare_json=summary["host_truth_paths"]["compare_json"],
        host_truth_compare_html=summary["host_truth_paths"]["compare_html"],
        expectations_json=summary["expectation_paths"]["baseline_json"],
        expectation_report_json=summary["expectation_paths"]["report_json"],
        expectation_report_html=summary["expectation_paths"]["report_html"],
        repro_compare_json=summary["repro_paths"]["compare_json"],
        repro_compare_html=summary["repro_paths"]["compare_html"],
        capture_image_report_json=summary["capture_audit_paths"]["image_report_json"],
        capture_meta_report_json=summary["capture_audit_paths"]["meta_report_json"],
        capture_semantic_report_json=summary["capture_audit_paths"]["semantic_report_json"],
        capture_report_json=summary["capture_audit_paths"]["capture_report_json"],
        image_baseline_json=summary["regression_baseline_paths"]["image_baseline_json"],
        meta_baseline_json=summary["regression_baseline_paths"]["meta_baseline_json"],
        semantic_baseline_json=summary["regression_baseline_paths"]["semantic_baseline_json"],
        fishing_scene_dir=summary["scene_root_paths"]["fishing_scene_dir"],
        mary_scene_dir=summary["scene_root_paths"]["mary_scene_dir"],
        fishing_frames_dir=summary["scene_asset_paths"]["fishing_frames_dir"],
        fishing_meta_dir=summary["scene_asset_paths"]["fishing_meta_dir"],
        mary_frames_dir=summary["scene_asset_paths"]["mary_frames_dir"],
        mary_meta_dir=summary["scene_asset_paths"]["mary_meta_dir"],
        fishing_start_bmp=summary["key_frame_paths"]["fishing_start_bmp"],
        fishing_late_bmp=summary["key_frame_paths"]["fishing_late_bmp"],
        mary_start_bmp=summary["key_frame_paths"]["mary_start_bmp"],
        mary_late_bmp=summary["key_frame_paths"]["mary_late_bmp"],
        fishing_start_json=summary["key_frame_meta_paths"]["fishing_start_json"],
        fishing_late_json=summary["key_frame_meta_paths"]["fishing_late_json"],
        mary_start_json=summary["key_frame_meta_paths"]["mary_start_json"],
        mary_late_json=summary["key_frame_meta_paths"]["mary_late_json"],
        identify_ratio=checks["identification-eval"]["min_best_to_second_ratio"],
        challenge_unknown_score=checks["identification-challenges"]["max_unknown_best_score"],
        challenge_unknown_margin=checks["identification-challenges"]["max_unknown_margin"],
        challenge_ambiguous_score=checks["identification-challenges"]["max_ambiguous_best_score"],
        challenge_ambiguous_margin=checks["identification-challenges"]["max_ambiguous_margin"],
        challenge_unknown_headroom=checks["identification-challenges"]["unknown_score_headroom"],
        challenge_unknown_margin_headroom=checks["identification-challenges"]["unknown_margin_headroom"],
        challenge_ambiguous_headroom=checks["identification-challenges"]["ambiguous_score_headroom"],
        challenge_ambiguous_margin_headroom=checks["identification-challenges"]["ambiguous_margin_headroom"],
        challenge_warn_count=checks["identification-challenges"]["warn_count"],
        challenge_danger_count=checks["identification-challenges"]["danger_count"],
        challenge_tightest=checks["identification-challenges"]["tightest_query_label"],
        challenge_tightest_metric=checks["identification-challenges"]["tightest_metric"],
        challenge_tightest_headroom=checks["identification-challenges"]["tightest_headroom"],
        challenge_tightest_pressure=checks["identification-challenges"]["tightest_pressure"],
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
assert_identification_challenges "$OUT_DIR/identification-challenges.json"
assert_identification_temporal "$OUT_DIR/identification-temporal.json"
assert_frame_image_regression_baseline "$OUT_DIR"
assert_frame_meta_regression_baseline "$OUT_DIR"
assert_identification_regression_floors "$OUT_DIR"
assert_semantic_regression_baseline "$OUT_DIR"
write_capture_regression_report "$OUT_DIR"
assert_capture_regression_report_consistency "$OUT_DIR"

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
assert_manifest_dashboard_links "$OUT_DIR"
assert_verification_summary_review_root "$OUT_DIR"
assert_verification_summary_absolute_paths "$OUT_DIR"
assert_verification_summary_existing_paths "$OUT_DIR"
assert_verification_summary_paths_under_root "$OUT_DIR"
assert_verification_summary_path_types "$OUT_DIR"
assert_verification_summary_unique_paths "$OUT_DIR"
assert_verification_summary_review_paths "$OUT_DIR"
assert_verification_summary_core_artifact_paths "$OUT_DIR"
assert_verification_summary_identification_audit_paths "$OUT_DIR"
assert_verification_summary_identification_floor_paths "$OUT_DIR"
assert_verification_summary_host_truth_paths "$OUT_DIR"
assert_verification_summary_expectation_paths "$OUT_DIR"
assert_verification_summary_repro_paths "$OUT_DIR"
assert_verification_summary_capture_audit_paths "$OUT_DIR"
assert_verification_summary_regression_baseline_paths "$OUT_DIR"
assert_verification_summary_scene_root_paths "$OUT_DIR"
assert_verification_summary_scene_asset_paths "$OUT_DIR"
assert_verification_summary_key_frame_paths "$OUT_DIR"
assert_verification_summary_key_frame_meta_paths "$OUT_DIR"
assert_verification_summary_text_paths "$OUT_DIR"
assert_verification_summary_artifact_input_coverage "$OUT_DIR"
assert_verification_summary_artifact_input_count "$OUT_DIR"
assert_verification_summary_path_map_count "$OUT_DIR"
assert_verification_summary_path_map_names "$OUT_DIR"
assert_verification_summary_path_map_entry_counts "$OUT_DIR"
assert_verification_summary_path_map_type_counts "$OUT_DIR"
assert_verification_summary_path_map_file_class_counts "$OUT_DIR"
assert_verification_summary_path_map_entry_names "$OUT_DIR"
assert_verification_summary_path_basenames "$OUT_DIR"
assert_verification_summary_path_relpaths "$OUT_DIR"
assert_verification_summary_path_depth_counts "$OUT_DIR"
assert_verification_summary_path_max_depth "$OUT_DIR"
assert_verification_summary_path_min_nonroot_depth "$OUT_DIR"
assert_verification_summary_path_entry_count "$OUT_DIR"
assert_verification_summary_path_type_counts "$OUT_DIR"
assert_verification_summary_path_class_counts "$OUT_DIR"
assert_dashboard_html_links "$OUT_DIR"
assert_identification_review_links "$OUT_DIR"
assert_capture_review_totals "$OUT_DIR"
assert_capture_review_tightest_drift "$OUT_DIR"
print_review_paths "$OUT_DIR"

rm -rf "$TMP_DIR"

echo "Host script review refreshed in $OUT_DIR"
