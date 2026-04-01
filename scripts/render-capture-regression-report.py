#!/usr/bin/env python3
import argparse
import json
import html
from pathlib import Path


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def summarize(report: dict, label_key: str) -> list[dict]:
    rows = []
    for scene in report.get("scenes", []):
        rows.append(
            {
                label_key: scene.get(label_key),
                "passed": scene.get("passed", False),
                "failure_count": scene.get("failure_count", 0),
            }
        )
    return rows


def render_html(payload: dict) -> str:
    def fmt_status(passed: bool) -> str:
        return "pass" if passed else "fail"

    def render_rows(name: str, label_key: str, rows: list[dict]) -> str:
        body = []
        for row in rows:
            label = row.get(label_key, "")
            status = fmt_status(bool(row.get("passed", False)))
            failure_count = row.get("failure_count", 0)
            path_hint = (
                f"{label.lower()}/frames" if label_key == "scene" else label.lower().replace(" ", "")
            )
            body.append(
                "<tr>"
                f"<td>{html.escape(str(label))}</td>"
                f"<td class=\"{status}\">{status.upper()}</td>"
                f"<td>{failure_count}</td>"
                f"<td>{html.escape(path_hint)}</td>"
                "</tr>"
            )
        return (
            f"<section><h2>{html.escape(name)}</h2>"
            "<table><thead><tr><th>Scene</th><th>Status</th><th>Failures</th><th>Path Hint</th></tr></thead>"
            f"<tbody>{''.join(body)}</tbody></table></section>"
        )

    checks = payload["checks"]
    sections = [
        render_rows("Frame Image", "scene", checks["frame-image"]["scenes"]),
        render_rows("Frame Meta", "scene", checks["frame-meta"]["scenes"]),
        render_rows("Semantic", "scene_label", checks["semantic"]["scenes"]),
    ]
    overall = fmt_status(bool(payload.get("passed", False)))
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
  </style>
</head>
<body>
  <main>
    <div class="summary">
      <h1>Capture Regression Review</h1>
      <div class="{overall}">Overall: {overall.upper()}</div>
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
    Path(args.out_json).write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    if args.out_html:
        Path(args.out_html).write_text(render_html(payload), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
