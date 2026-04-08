#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote


class ReviewHandler(SimpleHTTPRequestHandler):
    review_dir: Path
    annotations_path: Path

    def __init__(self, *args, directory: str | None = None, **kwargs):
        super().__init__(*args, directory=str(self.review_dir), **kwargs)

    def do_GET(self) -> None:
        if self.path == "/api/annotations":
            self._send_json(self._load_annotations())
            return
        fs_path = Path(unquote(self.path))
        if fs_path.is_absolute() and fs_path.is_file():
            self._send_file(fs_path)
            return
        super().do_GET()

    def do_POST(self) -> None:
        if self.path != "/api/save":
            self.send_error(HTTPStatus.NOT_FOUND, "Unknown endpoint")
            return
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        payload = json.loads(body.decode("utf-8"))
        self.annotations_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
        saved_count = sum(
            1
            for frame in payload.get("frames", [])
            if any(frame.get("labels", {}).values()) or str(frame.get("notes", "")).strip()
        )
        print(f"[scene-review] saved {saved_count} labeled frames -> {self.annotations_path}", flush=True)
        self._send_json({"ok": True, "saved_count": saved_count, "path": str(self.annotations_path)})

    def log_message(self, format: str, *args) -> None:
        print(f"[scene-review] {self.address_string()} - {format % args}", flush=True)

    def _load_annotations(self) -> dict:
        if not self.annotations_path.exists():
            return {"frames": []}
        return json.loads(self.annotations_path.read_text(encoding="utf-8"))

    def _send_json(self, payload: dict) -> None:
        body = (json.dumps(payload, indent=2) + "\n").encode("utf-8")
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_file(self, path: Path) -> None:
        body = path.read_bytes()
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Content-Type", self.guess_type(str(path)))
        self.end_headers()
        self.wfile.write(body)


def main() -> int:
    parser = argparse.ArgumentParser(description="Serve a local scene annotation review UI with autosave.")
    parser.add_argument("--review-dir", type=Path, required=True)
    parser.add_argument("--host", type=str, default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8123)
    args = parser.parse_args()

    review_dir = args.review_dir.resolve()
    annotations_path = review_dir / "annotations.json"
    ReviewHandler.review_dir = review_dir
    ReviewHandler.annotations_path = annotations_path

    server = ThreadingHTTPServer((args.host, args.port), ReviewHandler)
    url = f"http://{args.host}:{args.port}/review.html"
    print(url, flush=True)
    print(f"[scene-review] annotations -> {annotations_path}", flush=True)
    server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
