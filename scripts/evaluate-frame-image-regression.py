#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image


def summarize_frame(path: Path) -> dict:
    with Image.open(path) as img:
        rgba = img.convert("RGBA")
        full_pixel_sha256 = hashlib.sha256(rgba.tobytes()).hexdigest()
        if rgba.size == (640, 480):
            cropped = rgba.crop((0, 16, 640, 464))
        else:
            cropped = rgba
        for x0, y0, x1, y1 in (
            (0, 0, 110, 260),
            (500, 124, 618, 344),
            (548, 16, 616, 84),
        ):
            for y in range(y0, min(y1, cropped.height)):
                for x in range(x0, min(x1, cropped.width)):
                    cropped.putpixel((x, y), (0, 0, 0, 255))
        scene_pixel_sha256 = hashlib.sha256(cropped.tobytes()).hexdigest()
    return {
        "frame": path.stem,
        "full_pixel_sha256": full_pixel_sha256,
        "scene_pixel_sha256": scene_pixel_sha256,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Compare canonical frame images against a published baseline.")
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--out-json", required=True)
    args = ap.parse_args()

    baseline = json.loads(Path(args.baseline).read_text(encoding="utf-8"))
    root = Path(args.root)
    failures = []
    scenes = []
    for scene_name, expected_rows in baseline.get("scenes", {}).items():
        scene_failures = []
        scene_dir = root / scene_name / "frames"
        for expected in expected_rows:
            frame_name = expected["frame"]
            path = scene_dir / f"{frame_name}.bmp"
            if not path.is_file():
                scene_failures.append({"scene": scene_name, "frame": frame_name, "field": "missing_frame_image", "expected": True, "actual": False})
                continue
            current = summarize_frame(path)
            for key in ("full_pixel_sha256", "scene_pixel_sha256"):
                if current.get(key) != expected.get(key):
                    scene_failures.append(
                        {
                            "scene": scene_name,
                            "frame": frame_name,
                            "field": key,
                            "expected": expected.get(key),
                            "actual": current.get(key),
                            "frame_path": path.relative_to(root).as_posix(),
                        }
                    )
        failures.extend(scene_failures)
        scenes.append({"scene": scene_name, "passed": not scene_failures, "failure_count": len(scene_failures), "failures": scene_failures})

    payload = {
        "passed": not failures,
        "scene_count": len(scenes),
        "failure_count": len(failures),
        "scenes": scenes,
        "failures": failures,
    }
    Path(args.out_json).write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
