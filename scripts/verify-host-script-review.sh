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
import re
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

review_root = summary.get("review_root")
if review_root != str(root.resolve()):
    raise SystemExit("verification-summary review_root mismatch")
print(f"review-root: ok root={review_root}")

for key, value in summary.items():
    if key == "review_root":
        if not isinstance(value, str) or not Path(value).is_absolute():
            raise SystemExit("verification-summary review_root must be an absolute path")
        continue
    if not key.endswith("_paths"):
        continue
    if not isinstance(value, dict):
        raise SystemExit(f"verification-summary {key} must be a path map")
    for path_key, path_value in value.items():
        if not isinstance(path_value, str) or not Path(path_value).is_absolute():
            raise SystemExit(f"verification-summary {key}.{path_key} must be an absolute path")
print("summary-absolute-paths: ok")

for key, value in summary.items():
    if key == "review_root":
        if not Path(value).exists():
            raise SystemExit("verification-summary review_root does not exist")
        continue
    if not key.endswith("_paths"):
        continue
    for path_key, path_value in value.items():
        if not Path(path_value).exists():
            raise SystemExit(f"verification-summary {key}.{path_key} does not exist")
print("summary-existing-paths: ok")

review_root_path = Path(review_root).resolve()
for key, value in summary.items():
    if key == "review_root" or not key.endswith("_paths"):
        continue
    for path_key, path_value in value.items():
        try:
            Path(path_value).resolve().relative_to(review_root_path)
        except ValueError as exc:
            raise SystemExit(f"verification-summary {key}.{path_key} escapes review_root") from exc
print("summary-paths-under-root: ok")

if not review_root_path.is_dir():
    raise SystemExit("verification-summary review_root must be a directory")
for key, value in summary.items():
    if key == "review_root":
        continue
    if not key.endswith("_paths"):
        continue
    for path_key, path_value in value.items():
        path = Path(path_value)
        if path_key.endswith("_dir"):
            if not path.is_dir():
                raise SystemExit(f"verification-summary {key}.{path_key} must be a directory")
        else:
            if not path.is_file():
                raise SystemExit(f"verification-summary {key}.{path_key} must be a file")
print("summary-path-types: ok")

seen_paths = {str(review_root_path): "review_root"}
for key, value in summary.items():
    if not key.endswith("_paths"):
        continue
    for path_key, path_value in value.items():
        resolved = str(Path(path_value).resolve())
        prior = seen_paths.get(resolved)
        if prior is not None:
            raise SystemExit(f"verification-summary duplicate path: {prior} and {key}.{path_key}")
        seen_paths[resolved] = f"{key}.{path_key}"
print("summary-unique-paths: ok")

review_paths = summary.get("review_paths", {})
required_review_paths = {
    "index_html": root / "index.html",
    "identification_review_html": root / "identification-review.html",
    "capture_regression_review_html": root / "capture-regression-review.html",
}
for key, expected_path in required_review_paths.items():
    actual = review_paths.get(key)
    if actual != str(expected_path.resolve()):
        raise SystemExit(f"verification-summary review_paths.{key} mismatch")
print(
    "review-paths: ok "
    f"index={review_paths.get('index_html')} "
    f"identification={review_paths.get('identification_review_html')} "
    f"capture={review_paths.get('capture_regression_review_html')}"
)

core_artifact_paths = summary.get("core_artifact_paths", {})
required_core_artifact_paths = {
    "manifest_json": root / "manifest.json",
    "semantic_truth_json": root / "semantic-truth.json",
}
for key, expected_path in required_core_artifact_paths.items():
    actual = core_artifact_paths.get(key)
    if actual != str(expected_path.resolve()):
        raise SystemExit(f"verification-summary core_artifact_paths.{key} mismatch")
print(
    "core-artifact-paths: ok "
    f"manifest={core_artifact_paths.get('manifest_json')} "
    f"semantic={core_artifact_paths.get('semantic_truth_json')}"
)

identification_audit_paths = summary.get("identification_audit_paths", {})
required_identification_audit_paths = {
    "selfcheck_json": root / "identification-selfcheck.json",
    "eval_json": root / "identification-eval.json",
    "partials_json": root / "identification-partials.json",
    "challenges_json": root / "identification-challenges.json",
    "temporal_json": root / "identification-temporal.json",
}
for key, expected_path in required_identification_audit_paths.items():
    actual = identification_audit_paths.get(key)
    if actual != str(expected_path.resolve()):
        raise SystemExit(f"verification-summary identification_audit_paths.{key} mismatch")
print(
    "identification-audit-paths: ok "
    f"selfcheck={identification_audit_paths.get('selfcheck_json')} "
    f"eval={identification_audit_paths.get('eval_json')} "
    f"partials={identification_audit_paths.get('partials_json')} "
    f"challenges={identification_audit_paths.get('challenges_json')} "
    f"temporal={identification_audit_paths.get('temporal_json')}"
)

identification_floor_paths = summary.get("identification_floor_paths", {})
required_identification_floor_paths = {
    "regression_floors_json": root / "identification-regression-floors.json",
}
for key, expected_path in required_identification_floor_paths.items():
    actual = identification_floor_paths.get(key)
    if actual != str(expected_path.resolve()):
        raise SystemExit(f"verification-summary identification_floor_paths.{key} mismatch")
print(
    "identification-floor-paths: ok "
    f"floors={identification_floor_paths.get('regression_floors_json')}"
)

host_truth_paths = summary.get("host_truth_paths", {})
required_host_truth_paths = {
    "baseline_json": root / "host-truth-baseline.json",
    "compare_json": root / "host-truth-compare.json",
    "compare_html": root / "host-truth-compare.html",
}
for key, expected_path in required_host_truth_paths.items():
    actual = host_truth_paths.get(key)
    if actual != str(expected_path.resolve()):
        raise SystemExit(f"verification-summary host_truth_paths.{key} mismatch")
print(
    "host-truth-paths: ok "
    f"baseline={host_truth_paths.get('baseline_json')} "
    f"compare={host_truth_paths.get('compare_json')} "
    f"html={host_truth_paths.get('compare_html')}"
)

expectation_paths = summary.get("expectation_paths", {})
required_expectation_paths = {
    "baseline_json": root / "expectations.json",
    "report_json": root / "expectation-report.json",
    "report_html": root / "expectation-report.html",
}
for key, expected_path in required_expectation_paths.items():
    actual = expectation_paths.get(key)
    if actual != str(expected_path.resolve()):
        raise SystemExit(f"verification-summary expectation_paths.{key} mismatch")
print(
    "expectation-paths: ok "
    f"baseline={expectation_paths.get('baseline_json')} "
    f"report={expectation_paths.get('report_json')} "
    f"html={expectation_paths.get('report_html')}"
)

repro_paths = summary.get("repro_paths", {})
required_repro_paths = {
    "compare_json": root / "repro-compare.json",
    "compare_html": root / "repro-compare.html",
}
for key, expected_path in required_repro_paths.items():
    actual = repro_paths.get(key)
    if actual != str(expected_path.resolve()):
        raise SystemExit(f"verification-summary repro_paths.{key} mismatch")
print(
    "repro-paths: ok "
    f"compare={repro_paths.get('compare_json')} "
    f"html={repro_paths.get('compare_html')}"
)

capture_audit_paths = summary.get("capture_audit_paths", {})
required_capture_audit_paths = {
    "image_report_json": root / "frame-image-regression-report.json",
    "meta_report_json": root / "frame-meta-regression-report.json",
    "semantic_report_json": root / "semantic-regression-report.json",
    "capture_report_json": root / "capture-regression-report.json",
}
for key, expected_path in required_capture_audit_paths.items():
    actual = capture_audit_paths.get(key)
    if actual != str(expected_path.resolve()):
        raise SystemExit(f"verification-summary capture_audit_paths.{key} mismatch")
print(
    "capture-audit-paths: ok "
    f"image={capture_audit_paths.get('image_report_json')} "
    f"meta={capture_audit_paths.get('meta_report_json')} "
    f"semantic={capture_audit_paths.get('semantic_report_json')} "
    f"capture={capture_audit_paths.get('capture_report_json')}"
)

regression_baseline_paths = summary.get("regression_baseline_paths", {})
required_regression_baseline_paths = {
    "image_baseline_json": root / "frame-image-regression-baseline.json",
    "meta_baseline_json": root / "frame-meta-regression-baseline.json",
    "semantic_baseline_json": root / "semantic-regression-baseline.json",
}
for key, expected_path in required_regression_baseline_paths.items():
    actual = regression_baseline_paths.get(key)
    if actual != str(expected_path.resolve()):
        raise SystemExit(f"verification-summary regression_baseline_paths.{key} mismatch")
print(
    "regression-baseline-paths: ok "
    f"image={regression_baseline_paths.get('image_baseline_json')} "
    f"meta={regression_baseline_paths.get('meta_baseline_json')} "
    f"semantic={regression_baseline_paths.get('semantic_baseline_json')}"
)

scene_root_paths = summary.get("scene_root_paths", {})
required_scene_root_paths = {
    "fishing_scene_dir": root / "fishing1",
    "mary_scene_dir": root / "mary1",
}
for key, expected_path in required_scene_root_paths.items():
    actual = scene_root_paths.get(key)
    if actual != str(expected_path.resolve()):
        raise SystemExit(f"verification-summary scene_root_paths.{key} mismatch")
print(
    "scene-root-paths: ok "
    f"fishing={scene_root_paths.get('fishing_scene_dir')} "
    f"mary={scene_root_paths.get('mary_scene_dir')}"
)

scene_asset_paths = summary.get("scene_asset_paths", {})
required_scene_asset_paths = {
    "fishing_frames_dir": root / "fishing1" / "frames",
    "fishing_meta_dir": root / "fishing1" / "frame-meta",
    "mary_frames_dir": root / "mary1" / "frames",
    "mary_meta_dir": root / "mary1" / "frame-meta",
}
for key, expected_path in required_scene_asset_paths.items():
    actual = scene_asset_paths.get(key)
    if actual != str(expected_path.resolve()):
        raise SystemExit(f"verification-summary scene_asset_paths.{key} mismatch")
print(
    "scene-asset-paths: ok "
    f"fishing-frames={scene_asset_paths.get('fishing_frames_dir')} "
    f"fishing-meta={scene_asset_paths.get('fishing_meta_dir')} "
    f"mary-frames={scene_asset_paths.get('mary_frames_dir')} "
    f"mary-meta={scene_asset_paths.get('mary_meta_dir')}"
)

key_frame_paths = summary.get("key_frame_paths", {})
required_key_frame_paths = {
    "fishing_start_bmp": root / "fishing1" / "frames" / "frame_00000.bmp",
    "fishing_late_bmp": root / "fishing1" / "frames" / "frame_00080.bmp",
    "mary_start_bmp": root / "mary1" / "frames" / "frame_00000.bmp",
    "mary_late_bmp": root / "mary1" / "frames" / "frame_00100.bmp",
}
for key, expected_path in required_key_frame_paths.items():
    actual = key_frame_paths.get(key)
    if actual != str(expected_path.resolve()):
        raise SystemExit(f"verification-summary key_frame_paths.{key} mismatch")
print(
    "key-frame-paths: ok "
    f"fishing-start={key_frame_paths.get('fishing_start_bmp')} "
    f"fishing-late={key_frame_paths.get('fishing_late_bmp')} "
    f"mary-start={key_frame_paths.get('mary_start_bmp')} "
    f"mary-late={key_frame_paths.get('mary_late_bmp')}"
)

key_frame_meta_paths = summary.get("key_frame_meta_paths", {})
required_key_frame_meta_paths = {
    "fishing_start_json": root / "fishing1" / "frame-meta" / "frame_00000.json",
    "fishing_late_json": root / "fishing1" / "frame-meta" / "frame_00080.json",
    "mary_start_json": root / "mary1" / "frame-meta" / "frame_00000.json",
    "mary_late_json": root / "mary1" / "frame-meta" / "frame_00100.json",
}
for key, expected_path in required_key_frame_meta_paths.items():
    actual = key_frame_meta_paths.get(key)
    if actual != str(expected_path.resolve()):
        raise SystemExit(f"verification-summary key_frame_meta_paths.{key} mismatch")
print(
    "key-frame-meta-paths: ok "
    f"fishing-start={key_frame_meta_paths.get('fishing_start_json')} "
    f"fishing-late={key_frame_meta_paths.get('fishing_late_json')} "
    f"mary-start={key_frame_meta_paths.get('mary_start_json')} "
    f"mary-late={key_frame_meta_paths.get('mary_late_json')}"
)

summary_txt = (root / "verification-summary.txt").read_text(encoding="utf-8")
required_summary_txt_tokens = {
    f"review-root={review_root}",
    f"index={review_paths.get('index_html')}",
    f"identification={review_paths.get('identification_review_html')}",
    f"capture={review_paths.get('capture_regression_review_html')}",
    f"manifest-json={core_artifact_paths.get('manifest_json')}",
    f"semantic-truth-json={core_artifact_paths.get('semantic_truth_json')}",
    f"identify-selfcheck-json={identification_audit_paths.get('selfcheck_json')}",
    f"identify-eval-json={identification_audit_paths.get('eval_json')}",
    f"identify-partials-json={identification_audit_paths.get('partials_json')}",
    f"identify-challenges-json={identification_audit_paths.get('challenges_json')}",
    f"identify-temporal-json={identification_audit_paths.get('temporal_json')}",
    f"identify-regression-floors-json={identification_floor_paths.get('regression_floors_json')}",
    f"host-truth-baseline-json={host_truth_paths.get('baseline_json')}",
    f"host-truth-compare-json={host_truth_paths.get('compare_json')}",
    f"host-truth-compare-html={host_truth_paths.get('compare_html')}",
    f"expectations-json={expectation_paths.get('baseline_json')}",
    f"expectation-report-json={expectation_paths.get('report_json')}",
    f"expectation-report-html={expectation_paths.get('report_html')}",
    f"repro-compare-json={repro_paths.get('compare_json')}",
    f"repro-compare-html={repro_paths.get('compare_html')}",
    f"capture-image-report-json={capture_audit_paths.get('image_report_json')}",
    f"capture-meta-report-json={capture_audit_paths.get('meta_report_json')}",
    f"capture-semantic-report-json={capture_audit_paths.get('semantic_report_json')}",
    f"capture-report-json={capture_audit_paths.get('capture_report_json')}",
    f"image-baseline-json={regression_baseline_paths.get('image_baseline_json')}",
    f"meta-baseline-json={regression_baseline_paths.get('meta_baseline_json')}",
    f"semantic-baseline-json={regression_baseline_paths.get('semantic_baseline_json')}",
    f"fishing-scene-dir={scene_root_paths.get('fishing_scene_dir')}",
    f"mary-scene-dir={scene_root_paths.get('mary_scene_dir')}",
    f"fishing-frames-dir={scene_asset_paths.get('fishing_frames_dir')}",
    f"fishing-meta-dir={scene_asset_paths.get('fishing_meta_dir')}",
    f"mary-frames-dir={scene_asset_paths.get('mary_frames_dir')}",
    f"mary-meta-dir={scene_asset_paths.get('mary_meta_dir')}",
    f"fishing-start-bmp={key_frame_paths.get('fishing_start_bmp')}",
    f"fishing-late-bmp={key_frame_paths.get('fishing_late_bmp')}",
    f"mary-start-bmp={key_frame_paths.get('mary_start_bmp')}",
    f"mary-late-bmp={key_frame_paths.get('mary_late_bmp')}",
    f"fishing-start-json={key_frame_meta_paths.get('fishing_start_json')}",
    f"fishing-late-json={key_frame_meta_paths.get('fishing_late_json')}",
    f"mary-start-json={key_frame_meta_paths.get('mary_start_json')}",
    f"mary-late-json={key_frame_meta_paths.get('mary_late_json')}",
}
for token in required_summary_txt_tokens:
    if token not in summary_txt:
        raise SystemExit(f"verification-summary.txt missing token: {token}")

manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
manifest_extras = manifest.get("extras", {})
required_manifest_extras = {
    "identification-review.html": root / "identification-review.html",
    "capture-regression-review.html": root / "capture-regression-review.html",
    "capture-regression-report.json": root / "capture-regression-report.json",
    "verification-summary.json": root / "verification-summary.json",
    "verification-summary.txt": root / "verification-summary.txt",
    "semantic-truth.json": root / "semantic-truth.json",
}
for key, expected_path in required_manifest_extras.items():
    actual = manifest_extras.get(key)
    if actual != str(expected_path.resolve()):
        raise SystemExit(f"manifest extras.{key} mismatch")

index_html = (root / "index.html").read_text(encoding="utf-8")
for href in (
    "identification-review.html",
    "capture-regression-review.html",
    "capture-regression-report.json",
    "verification-summary.json",
    "verification-summary.txt",
    "semantic-truth.json",
):
    if href not in index_html:
        raise SystemExit(f"index.html missing link: {href}")

capture_html = (root / "capture-regression-review.html").read_text(encoding="utf-8")
for href in (
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
):
    if href not in capture_html:
        raise SystemExit(f"capture-regression-review.html missing link: {href}")
for label, value in (
    ("Frame Image Failures", capture_regression.get("totals", {}).get("frame_image_failures", 0)),
    ("Frame Meta Failures", capture_regression.get("totals", {}).get("frame_meta_failures", 0)),
    ("Semantic Failures", capture_regression.get("totals", {}).get("semantic_failures", 0)),
):
    pattern = rf"{re.escape(label)}</div>\s*<div class=\"value [^\"]+\">{re.escape(str(value))}</div>"
    if not re.search(pattern, capture_html):
        raise SystemExit(f"capture-regression-review.html totals mismatch: {label}={value}")
tightest = capture_regression.get("tightest_drift")
if tightest:
    scene = tightest.get("scene") or tightest.get("scene_label") or ""
    field = tightest.get("field") or ""
    check = tightest.get("check") or ""
    pattern = rf"Tightest Drift</h2>\s*<div>{re.escape(str(check))} / {re.escape(str(scene))} / {re.escape(str(field))}"
    if not re.search(pattern, capture_html):
        raise SystemExit("capture-regression-review.html tightest drift summary mismatch")
    frame = tightest.get("frame")
    if frame:
        scene_slug = str(scene).lower().replace(" ", "")
        for href in (
            f"{scene_slug}/frames/{frame}.bmp",
            f"{scene_slug}/frame-meta/{frame}.json",
        ):
            if href not in capture_html:
                raise SystemExit(f"capture-regression-review.html tightest drift asset missing: {href}")

identification_html = (root / "identification-review.html").read_text(encoding="utf-8")
for href in (
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
):
    if href not in identification_html:
        raise SystemExit(f"identification-review.html missing link: {href}")

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

print(
    "capture-regression-report: ok "
    f"frame_image_failures={capture_totals.get('frame_image_failures')} "
    f"frame_meta_failures={capture_totals.get('frame_meta_failures')} "
    f"semantic_failures={capture_totals.get('semantic_failures')} "
    f"first_image_scene={capture_first_failed.get('frame-image')} "
    f"first_meta_scene={capture_first_failed.get('frame-meta')} "
    f"first_semantic_scene={capture_first_failed.get('semantic')}"
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

for key, value in summary.items():
    if not key.endswith("_paths"):
        continue
    for path_key, path_value in value.items():
        if path_key.endswith("_dir"):
            continue
        rel = Path(path_value).resolve().relative_to(root.resolve()).as_posix()
        if rel not in artifact_inputs:
            raise SystemExit(f"artifact_inputs missing summary file path: {key}.{path_key}")

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
