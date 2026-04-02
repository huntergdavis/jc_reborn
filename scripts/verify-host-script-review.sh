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
    "identification-partials.json",
    "identification-challenges.json",
    "identification-temporal.json",
    "frame-image-regression-baseline.json",
    "frame-image-regression-report.json",
    "frame-meta-regression-baseline.json",
    "frame-meta-regression-report.json",
    "capture-regression-report.json",
    "capture-regression-review.html",
    "identification-regression-floors.json",
    "semantic-regression-baseline.json",
    "semantic-regression-report.json",
    "identification-review.html",
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
    f"max_nonmatch_score={identify_eval.get('max_nonmatch_score')} "
    f"min_ratio={identify_eval.get('min_best_to_second_ratio')}"
)

identify_partials = json.loads((root / "identification-partials.json").read_text(encoding="utf-8"))
if not identify_partials.get("passed"):
    raise SystemExit(
        "identification-partials failed: " + "; ".join(identify_partials.get("failures", []))
    )
print(
    "identification-partials: ok "
    f"query_count={identify_partials.get('query_count')} "
    f"min_margin={identify_partials.get('min_margin')} "
    f"min_ratio={identify_partials.get('min_best_to_second_ratio')}"
)

identify_challenges = json.loads((root / "identification-challenges.json").read_text(encoding="utf-8"))
if not identify_challenges.get("passed"):
    raise SystemExit(
        "identification-challenges failed: " + "; ".join(identify_challenges.get("failures", []))
    )
print(
    "identification-challenges: ok "
    f"query_count={identify_challenges.get('query_count')} "
    f"max_best_score={identify_challenges.get('max_best_score')} "
    f"max_margin={identify_challenges.get('max_margin')} "
    f"ambiguous={identify_challenges.get('ambiguous_count')} "
    f"unknown={identify_challenges.get('unknown_count')}"
)

identify_temporal = json.loads((root / "identification-temporal.json").read_text(encoding="utf-8"))
if not identify_temporal.get("passed"):
    raise SystemExit(
        "identification-temporal failed: " + "; ".join(identify_temporal.get("failures", []))
    )
print(
    "identification-temporal: ok "
    f"query_count={identify_temporal.get('query_count')} "
    f"min_margin={identify_temporal.get('min_identified_margin')} "
    f"min_ratio={identify_temporal.get('min_identified_ratio')} "
    f"max_score_drop={identify_temporal.get('max_score_drop')} "
    f"max_margin_drop={identify_temporal.get('max_identified_margin_drop')}"
)

frame_image_regression = json.loads((root / "frame-image-regression-report.json").read_text(encoding="utf-8"))
if not frame_image_regression.get("passed", False):
    frame_image_failures = []
    for failure in frame_image_regression.get("failures", []):
        frame_image_failures.append(f"{failure.get('scene')} {failure.get('frame')} {failure.get('field')} drifted")
    raise SystemExit("frame-image-regression-baseline failed: " + "; ".join(frame_image_failures))
print("frame-image-regression-baseline: ok")

frame_meta_regression = json.loads((root / "frame-meta-regression-report.json").read_text(encoding="utf-8"))
if not frame_meta_regression.get("passed", False):
    frame_meta_failures = []
    for failure in frame_meta_regression.get("failures", []):
        frame_meta_failures.append(f"{failure.get('scene')} {failure.get('frame')} {failure.get('field')} drifted")
    raise SystemExit("frame-meta-regression-baseline failed: " + "; ".join(frame_meta_failures))
print("frame-meta-regression-baseline: ok")

floors = json.loads((root / "identification-regression-floors.json").read_text(encoding="utf-8"))
regression_failures = []
if float(identify_eval.get("min_identified_margin", 0.0)) < float(floors["identification-eval"]["min_identified_margin"]):
    regression_failures.append("identification-eval min_identified_margin below floor")
if float(identify_eval.get("min_best_to_second_ratio", 0.0)) < float(floors["identification-eval"]["min_best_to_second_ratio"]):
    regression_failures.append("identification-eval min_best_to_second_ratio below floor")
if float(identify_eval.get("max_nonmatch_score", 0.0)) > float(floors["identification-eval"]["max_nonmatch_score"]):
    regression_failures.append("identification-eval max_nonmatch_score above ceiling")
if float(identify_partials.get("min_margin", 0.0)) < float(floors["identification-partials"]["min_margin"]):
    regression_failures.append("identification-partials min_margin below floor")
if float(identify_partials.get("min_best_to_second_ratio", 0.0)) < float(floors["identification-partials"]["min_best_to_second_ratio"]):
    regression_failures.append("identification-partials min_best_to_second_ratio below floor")
if float(identify_temporal.get("min_identified_margin", 0.0)) < float(floors["identification-temporal"]["min_identified_margin"]):
    regression_failures.append("identification-temporal min_identified_margin below floor")
if float(identify_temporal.get("min_identified_ratio", 0.0)) < float(floors["identification-temporal"]["min_identified_ratio"]):
    regression_failures.append("identification-temporal min_identified_ratio below floor")
if float(identify_temporal.get("max_score_drop", 0.0)) > float(floors["identification-temporal"]["max_score_drop"]):
    regression_failures.append("identification-temporal max_score_drop above ceiling")
if float(identify_temporal.get("max_identified_margin_drop", 0.0)) > float(floors["identification-temporal"]["max_identified_margin_drop"]):
    regression_failures.append("identification-temporal max_identified_margin_drop above ceiling")
if float(identify_challenges.get("max_best_score", 0.0)) > float(floors["identification-challenges"]["max_best_score"]):
    regression_failures.append("identification-challenges max_best_score above ceiling")
if float(identify_challenges.get("max_margin", 0.0)) > float(floors["identification-challenges"]["max_margin"]):
    regression_failures.append("identification-challenges max_margin above ceiling")
if regression_failures:
    raise SystemExit("identification-regression-floors failed: " + "; ".join(regression_failures))
print("identification-regression-floors: ok")

semantic_report = json.loads((root / "semantic-regression-report.json").read_text(encoding="utf-8"))
if not semantic_report.get("passed", False):
    semantic_failures = []
    for failure in semantic_report.get("failures", []):
        scene = failure.get("scene_label")
        frame = failure.get("frame_number")
        field = failure.get("field")
        if frame is None:
            semantic_failures.append(f"{scene} {field} drifted")
        else:
            semantic_failures.append(f"{scene} frame {frame} {field} drifted")
    raise SystemExit("semantic-regression-baseline failed: " + "; ".join(semantic_failures))
print("semantic-regression-baseline: ok")

capture_regression = json.loads((root / "capture-regression-report.json").read_text(encoding="utf-8"))
if not capture_regression.get("passed", False):
    raise SystemExit("capture-regression-report failed")

capture_checks = capture_regression.get("checks", {})
capture_totals = capture_regression.get("totals", {})
capture_first_failed = capture_regression.get("first_failed_scenes", {})

frame_image_expected = {
    "passed": frame_image_regression.get("passed", False),
    "failure_count": frame_image_regression.get("failure_count", 0),
}
frame_meta_expected = {
    "passed": frame_meta_regression.get("passed", False),
    "failure_count": frame_meta_regression.get("failure_count", 0),
}
semantic_expected = {
    "passed": semantic_report.get("passed", False),
    "failure_count": semantic_report.get("failure_count", 0),
}

for key, expected in (
    ("frame-image", frame_image_expected),
    ("frame-meta", frame_meta_expected),
    ("semantic", semantic_expected),
):
    actual = capture_checks.get(key, {})
    if bool(actual.get("passed", False)) != bool(expected["passed"]):
        raise SystemExit(f"capture-regression-report {key} passed mismatch")
    if int(actual.get("failure_count", 0)) != int(expected["failure_count"]):
        raise SystemExit(f"capture-regression-report {key} failure_count mismatch")

def first_failed_scene(report, label_key):
    for scene in report.get("scenes", []):
        if not scene.get("passed", False):
            value = scene.get(label_key)
            return None if value in (None, "") else str(value)
    return None

if capture_first_failed.get("frame-image") != first_failed_scene(frame_image_regression, "scene"):
    raise SystemExit("capture-regression-report frame-image first_failed_scene mismatch")
if capture_first_failed.get("frame-meta") != first_failed_scene(frame_meta_regression, "scene"):
    raise SystemExit("capture-regression-report frame-meta first_failed_scene mismatch")
if capture_first_failed.get("semantic") != first_failed_scene(semantic_report, "scene_label"):
    raise SystemExit("capture-regression-report semantic first_failed_scene mismatch")

if int(capture_totals.get("frame_image_failures", 0)) != int(frame_image_regression.get("failure_count", 0)):
    raise SystemExit("capture-regression-report frame_image_failures mismatch")
if int(capture_totals.get("frame_meta_failures", 0)) != int(frame_meta_regression.get("failure_count", 0)):
    raise SystemExit("capture-regression-report frame_meta_failures mismatch")
if int(capture_totals.get("semantic_failures", 0)) != int(semantic_report.get("failure_count", 0)):
    raise SystemExit("capture-regression-report semantic_failures mismatch")

print("capture-regression-report: ok")

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
if eval_summary.get("min_best_to_second_ratio") != identify_eval.get("min_best_to_second_ratio"):
    raise SystemExit("summary min_best_to_second_ratio mismatch for identification-eval")

partials_summary = ((summary.get("checks") or {}).get("identification-partials") or {})
if not partials_summary.get("present") or not partials_summary.get("passed"):
    raise SystemExit("summary reports identification-partials failure")
if int(partials_summary.get("query_count", 0)) != int(identify_partials.get("query_count", 0)):
    raise SystemExit("summary query_count mismatch for identification-partials")
if partials_summary.get("min_margin") != identify_partials.get("min_margin"):
    raise SystemExit("summary min_margin mismatch for identification-partials")
if partials_summary.get("min_best_to_second_ratio") != identify_partials.get("min_best_to_second_ratio"):
    raise SystemExit("summary min_best_to_second_ratio mismatch for identification-partials")

challenges_summary = ((summary.get("checks") or {}).get("identification-challenges") or {})
if not challenges_summary.get("present") or not challenges_summary.get("passed"):
    raise SystemExit("summary reports identification-challenges failure")
if int(challenges_summary.get("query_count", 0)) != int(identify_challenges.get("query_count", 0)):
    raise SystemExit("summary query_count mismatch for identification-challenges")
if challenges_summary.get("max_best_score") != identify_challenges.get("max_best_score"):
    raise SystemExit("summary max_best_score mismatch for identification-challenges")
if challenges_summary.get("max_margin") != identify_challenges.get("max_margin"):
    raise SystemExit("summary max_margin mismatch for identification-challenges")
if int(challenges_summary.get("ambiguous_count", 0)) != int(identify_challenges.get("ambiguous_count", 0)):
    raise SystemExit("summary ambiguous_count mismatch for identification-challenges")
if int(challenges_summary.get("unknown_count", 0)) != int(identify_challenges.get("unknown_count", 0)):
    raise SystemExit("summary unknown_count mismatch for identification-challenges")

temporal_summary = ((summary.get("checks") or {}).get("identification-temporal") or {})
if not temporal_summary.get("present") or not temporal_summary.get("passed"):
    raise SystemExit("summary reports identification-temporal failure")
if int(temporal_summary.get("query_count", 0)) != int(identify_temporal.get("query_count", 0)):
    raise SystemExit("summary query_count mismatch for identification-temporal")
if temporal_summary.get("min_identified_margin") != identify_temporal.get("min_identified_margin"):
    raise SystemExit("summary min_identified_margin mismatch for identification-temporal")
if temporal_summary.get("min_identified_ratio") != identify_temporal.get("min_identified_ratio"):
    raise SystemExit("summary min_identified_ratio mismatch for identification-temporal")
if temporal_summary.get("max_score_drop") != identify_temporal.get("max_score_drop"):
    raise SystemExit("summary max_score_drop mismatch for identification-temporal")
if temporal_summary.get("max_identified_margin_drop") != identify_temporal.get("max_identified_margin_drop"):
    raise SystemExit("summary max_identified_margin_drop mismatch for identification-temporal")

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
