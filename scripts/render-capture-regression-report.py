#!/usr/bin/env python3
import argparse
import json
import html
from pathlib import Path


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def resolve_report_path(path_value: str | None, base_dir: Path | None) -> str | None:
    if not path_value:
        return None
    path = Path(path_value)
    if path.is_absolute():
        return str(path.resolve())
    if base_dir is None:
        return path_value
    return str((base_dir / path).resolve())


def summarize(report: dict, label_key: str) -> list[dict]:
    rows = []
    for scene in report.get("scenes", []):
        failures = scene.get("failures", [])
        first_failure = failures[0] if failures else None
        rows.append(
            {
                label_key: scene.get(label_key),
                "passed": scene.get("passed", False),
                "failure_count": scene.get("failure_count", 0),
                "first_failure": first_failure,
            }
        )
    return rows


def first_scene_failure(rows: list[dict], label_key: str) -> str | None:
    for row in rows:
        if not row.get("passed", False):
            value = row.get(label_key)
            return None if value in (None, "") else str(value)
    return None


def render_html(payload: dict) -> str:
    def fmt_status(passed: bool) -> str:
        return "pass" if passed else "fail"

    def shorten(value) -> str:
        text = json.dumps(value, sort_keys=True) if isinstance(value, (dict, list)) else str(value)
        if len(text) > 120:
            return text[:117] + "..."
        return text

    def slugify_scene(label: object, label_key: str) -> str:
        text = "" if label in (None, "") else str(label)
        return text if label_key == "scene" else text.lower().replace(" ", "")

    def meta_root(scene_slug: str, failure: dict) -> str:
        meta_path = failure.get("meta_path")
        if meta_path:
            try:
                return str(Path(meta_path).parent)
            except TypeError:
                pass
        frame_path = failure.get("frame_path")
        if frame_path:
            try:
                parent = Path(frame_path).parent
                if parent.name == "frames":
                    return str(parent.parent / "frame-meta")
                return str(parent)
            except TypeError:
                pass
        return f"{scene_slug}/frame-meta"

    def normalize_failure_paths(failure: dict, check_name: str) -> dict:
        if not failure:
            return {}
        source_roots = payload.get("source_roots", {})
        base_dir_value = source_roots.get(check_name)
        base_dir = Path(base_dir_value) if base_dir_value else None
        out = dict(failure)
        if "frame_path" in out:
            out["frame_path"] = resolve_report_path(out.get("frame_path"), base_dir)
        if "meta_path" in out:
            out["meta_path"] = resolve_report_path(out.get("meta_path"), base_dir)
        return out

    report_links = (
        '<div class="links">'
        '<a href="index.html">index.html</a> '
        '<a href="identification-review.html">identification-review.html</a> '
        '<a href="verification-summary.json">verification-summary.json</a> '
        '<a href="verification-summary.txt">verification-summary.txt</a> '
        '<a href="semantic-truth.json">semantic-truth.json</a> '
        '<a href="frame-image-regression-report.json">frame-image-regression-report.json</a> '
        '<a href="frame-meta-regression-report.json">frame-meta-regression-report.json</a> '
        '<a href="semantic-regression-report.json">semantic-regression-report.json</a> '
        '<a href="capture-regression-report.json">capture-regression-report.json</a>'
        "</div>"
    )

    tightest = payload.get("tightest_drift") or {}
    checks = payload.get("checks", {})

    def render_rows(name: str, label_key: str, rows: list[dict]) -> str:
        body = []
        for row in rows:
            label = row.get(label_key, "")
            status = fmt_status(bool(row.get("passed", False)))
            failure_count = row.get("failure_count", 0)
            first_failure = normalize_failure_paths(row.get("first_failure") or {}, name.lower().replace(" ", "-"))
            scene_slug = slugify_scene(label, label_key)
            frame_href = f"{scene_slug}/frames/"
            meta_href = f"{scene_slug}/frame-meta/"
            drift_links = ""
            if failure_count:
                failure_preview = " ".join(
                    str(part)
                    for part in (
                        first_failure.get("frame"),
                        first_failure.get("field"),
                    )
                    if part not in (None, "")
                )
                failure_detail = (
                    f"expected={shorten(first_failure.get('expected'))} "
                    f"actual={shorten(first_failure.get('actual'))}"
                )
                frame_name = first_failure.get("frame")
                if frame_name:
                    root_dir = meta_root(scene_slug, first_failure)
                    links = []
                    frame_href = first_failure.get("frame_path") or f"{scene_slug}/frames/{frame_name}.bmp"
                    links.append(f'<a href="{html.escape(frame_href)}">frame</a>')
                    meta_href = first_failure.get("meta_path") or f"{root_dir}/{frame_name}.json"
                    links.append(f'<a href="{html.escape(meta_href)}">meta</a>')
                    drift_links = " ".join(links)
            else:
                failure_preview = ""
                failure_detail = ""
            body.append(
                "<tr>"
                f"<td>{html.escape(str(label))}</td>"
                f"<td class=\"{status}\">{status.upper()}</td>"
                f"<td>{failure_count}</td>"
                f"<td>{html.escape(failure_preview)}</td>"
                f"<td>{html.escape(failure_detail)}</td>"
                f"<td>{drift_links}</td>"
                f"<td><a href=\"{html.escape(frame_href)}\">frames</a> <a href=\"{html.escape(meta_href)}\">frame-meta</a></td>"
                "</tr>"
            )
        return (
            f"<section><h2>{html.escape(name)}</h2>"
            "<table><thead><tr><th>Scene</th><th>Status</th><th>Failures</th><th>First Drift</th><th>Detail</th><th>Drift Assets</th><th>Links</th></tr></thead>"
            f"<tbody>{''.join(body)}</tbody></table></section>"
        )

    checks = payload["checks"]
    sections = [
        render_rows("Frame Image", "scene", checks["frame-image"]["scenes"]),
        render_rows("Frame Meta", "scene", checks["frame-meta"]["scenes"]),
        render_rows("Semantic", "scene_label", checks["semantic"]["scenes"]),
    ]
    overall = fmt_status(bool(payload.get("passed", False)))
    tightest_html = ""
    if tightest:
        tightest = normalize_failure_paths(tightest, str(tightest.get("check") or ""))
        scene_name = tightest.get("scene") or tightest.get("scene_label") or ""
        scene_slug = str(scene_name).lower().replace(" ", "")
        frame_name = tightest.get("frame") or ""
        drift_links = ""
        if frame_name and scene_slug:
            root_dir = meta_root(scene_slug, tightest)
            frame_href = tightest.get("frame_path") or f"{scene_slug}/frames/{frame_name}.bmp"
            meta_href = tightest.get("meta_path") or f"{root_dir}/{frame_name}.json"
            drift_links = (
                f' <a href="{html.escape(frame_href)}">frame</a>'
                f' <a href="{html.escape(meta_href)}">meta</a>'
            )
        tightest_html = (
            "<div class=\"summary\">"
            "<h2>Tightest Drift</h2>"
            f"<div>{html.escape(str(tightest.get('check', '')))} / "
            f"{html.escape(str(scene_name))} / "
            f"{html.escape(str(tightest.get('field', '')))}"
            f"{drift_links}</div>"
            "</div>"
        )
    return f"""<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>Capture Regression Review</title>
  <style>
    body {{ font: 14px/1.4 monospace; margin: 0; background: #0b0f14; color: #e6edf3; }}
    main {{ max-width: 1100px; margin: 0 auto; padding: 24px; }}
    .summary {{ padding: 12px 16px; border: 1px solid #223; border-radius: 8px; background: #111826; margin-bottom: 20px; }}
    .pass {{ color: #7ee787; }}
    .fail {{ color: #ff7b72; }}
    table {{ width: 100%; border-collapse: collapse; margin-bottom: 24px; background: #111826; }}
    th, td {{ border: 1px solid #223; padding: 8px 10px; text-align: left; }}
    h1, h2 {{ margin: 0 0 12px 0; }}
    h2 {{ margin-top: 20px; }}
    a {{ color: #8bd5ff; text-decoration: none; margin-right: 12px; }}
    .links {{ margin-top: 10px; }}
    .cards {{ display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 12px; margin: 20px 0; }}
    .card {{ padding: 12px 16px; border: 1px solid #223; border-radius: 8px; background: #111826; }}
    .card .label {{ color: #9fb0c0; margin-bottom: 6px; }}
    .card .value {{ font-size: 20px; }}
  </style>
</head>
<body>
  <main>
    <div class="summary">
      <h1>Capture Regression Review</h1>
      <div class="{overall}">Overall: {overall.upper()}</div>
      {report_links}
    </div>
    {tightest_html}
    <div class="cards">
      <div class="card">
        <div class="label">Frame Image Failures</div>
        <div class="value {'pass' if checks.get('frame-image', {}).get('failure_count', 0) == 0 else 'fail'}">{checks.get('frame-image', {}).get('failure_count', 0)}</div>
      </div>
      <div class="card">
        <div class="label">Frame Meta Failures</div>
        <div class="value {'pass' if checks.get('frame-meta', {}).get('failure_count', 0) == 0 else 'fail'}">{checks.get('frame-meta', {}).get('failure_count', 0)}</div>
      </div>
      <div class="card">
        <div class="label">Semantic Failures</div>
        <div class="value {'pass' if checks.get('semantic', {}).get('failure_count', 0) == 0 else 'fail'}">{checks.get('semantic', {}).get('failure_count', 0)}</div>
      </div>
    </div>
    {''.join(sections)}
  </main>
</body>
</html>
"""


def main() -> int:
    ap = argparse.ArgumentParser(description="Render a consolidated capture regression report.")
    ap.add_argument("--frame-image", required=True)
    ap.add_argument("--frame-meta", required=True)
    ap.add_argument("--semantic", required=True)
    ap.add_argument("--out-json", required=True)
    ap.add_argument("--out-html")
    args = ap.parse_args()

    frame_image = load(Path(args.frame_image))
    frame_meta = load(Path(args.frame_meta))
    semantic = load(Path(args.semantic))

    payload = {
        "passed": all(
            report.get("passed", False)
            for report in (frame_image, frame_meta, semantic)
        ),
        "source_roots": {
            "frame-image": str(Path(args.frame_image).resolve().parent),
            "frame-meta": str(Path(args.frame_meta).resolve().parent),
            "semantic": str(Path(args.semantic).resolve().parent),
        },
        "checks": {
            "frame-image": {
                "passed": frame_image.get("passed", False),
                "failure_count": frame_image.get("failure_count", 0),
                "scenes": summarize(frame_image, "scene"),
            },
            "frame-meta": {
                "passed": frame_meta.get("passed", False),
                "failure_count": frame_meta.get("failure_count", 0),
                "scenes": summarize(frame_meta, "scene"),
            },
            "semantic": {
                "passed": semantic.get("passed", False),
                "failure_count": semantic.get("failure_count", 0),
                "scenes": summarize(semantic, "scene_label"),
            },
        },
    }
    payload["totals"] = {
        "frame_image_failures": payload["checks"]["frame-image"]["failure_count"],
        "frame_meta_failures": payload["checks"]["frame-meta"]["failure_count"],
        "semantic_failures": payload["checks"]["semantic"]["failure_count"],
    }
    payload["first_failed_scenes"] = {
        "frame-image": first_scene_failure(payload["checks"]["frame-image"]["scenes"], "scene"),
        "frame-meta": first_scene_failure(payload["checks"]["frame-meta"]["scenes"], "scene"),
        "semantic": first_scene_failure(payload["checks"]["semantic"]["scenes"], "scene_label"),
    }
    for check_name, report in (
        ("frame-image", frame_image),
        ("frame-meta", frame_meta),
        ("semantic", semantic),
    ):
        failures = report.get("failures", [])
        if failures:
            first = dict(failures[0])
            first["check"] = check_name
            payload["tightest_drift"] = first
            break
    Path(args.out_json).write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    if args.out_html:
        Path(args.out_html).write_text(render_html(payload), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
