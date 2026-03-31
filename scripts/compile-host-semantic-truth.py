#!/usr/bin/env python3
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path


def load_summarize():
    script_path = Path(__file__).with_name("summarize-frame-meta.py")
    spec = importlib.util.spec_from_file_location("summarize_frame_meta", script_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load summarizer from {script_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.summarize


summarize = load_summarize()


def scene_family(scene_label: str) -> str:
    upper = (scene_label or "").upper()
    if upper.startswith("FISHING"):
        return "fishing"
    if upper.startswith("MARY"):
        return "mary"
    return "unknown"


def context_labels(visible_draws: list[dict]) -> list[str]:
    upper = [str(row.get("bmp_name") or "").upper() for row in visible_draws]
    labels: list[str] = []
    if any(name.startswith("MRAFT") or name.startswith("MJRAFT") or name.startswith("SJRAFT") for name in upper):
        labels.append("raft_visible")
    if any(name.startswith("TRUNK") for name in upper):
        labels.append("tree_visible")
    if any(name.startswith("BACKGRND") for name in upper):
        labels.append("background_visible")
    return labels


def classify_pose_labels(bmp_names: list[str], entities: list[str], family: str) -> list[str]:
    upper = [name.upper() for name in bmp_names]
    labels: list[str] = []
    if not entities:
        labels.append("no_actor_visible")
    else:
        labels.append("actor_visible")
    if "johnny" in entities:
        labels.append("johnny_visible")
    if "mary" in entities:
        labels.append("mary_visible")
    if any(name.startswith("MJFISH") or name.startswith("GJCATCH") for name in upper):
        labels.append("johnny_fishing_pose")
    if any(name == "JOHNWALK.BMP" for name in upper):
        labels.append("johnny_walking_pose")
    if any(name == "MJREAD.BMP" for name in upper):
        labels.append("johnny_reading_pose")
    if any(name.startswith("SMDATE") or name.startswith("SASKDATE") for name in upper):
        labels.append("date_scene_pose")
    if any(name.startswith("SBREAKUP") or name.startswith("SJBRAKUP") for name in upper):
        labels.append("breakup_scene_pose")
    if family == "fishing" and "johnny_visible" in labels and "johnny_fishing_pose" not in labels:
        labels.append("fishing_scene_actor_present")
    if family == "mary" and "johnny_visible" in labels and "mary_visible" not in labels:
        labels.append("mary_scene_johnny_only")
    return labels


def classify_frame_state(entities: list[str], pose_labels: list[str], family: str) -> str:
    if not entities:
        return "background_only"
    if "mary" in entities and "johnny" in entities:
        return "johnny_mary_pair"
    if "mary" in entities:
        return "mary_only"
    if "johnny_fishing_pose" in pose_labels:
        return "johnny_fishing"
    if "johnny" in entities and family == "mary":
        return "johnny_in_mary_scene"
    if "johnny" in entities:
        return "johnny_only"
    return "actor_present_other"


def semantic_confidence(frame_state: str, actor_count: int, bmp_names: list[str]) -> str:
    if frame_state == "background_only":
        return "high"
    if actor_count == 0:
        return "high"
    if actor_count == 1 and len(set(bmp_names)) == 1:
        return "high"
    if actor_count >= 2:
        return "medium"
    return "medium"


def semantic_reasons(
    family: str,
    frame_state: str,
    entities: list[str],
    bmp_names: list[str],
    ctx_labels: list[str],
) -> list[str]:
    reasons: list[str] = []
    if not entities:
        reasons.append("no trusted actor BMPs in deduped visible draws")
    else:
        reasons.append("trusted actor BMPs detected: " + ", ".join(bmp_names))
    if family != "unknown":
        reasons.append(f"scene_family={family}")
    if ctx_labels:
        reasons.append("context=" + ",".join(ctx_labels))
    reasons.append(f"frame_state={frame_state}")
    return reasons


def frame_region(summary: dict) -> dict | None:
    actors = summary.get("actor_candidates") or []
    if not actors:
        return None
    xs = [int(row["x"]) for row in actors]
    ys = [int(row["y"]) for row in actors]
    x2 = [int(row["x"]) + int(row["width"]) for row in actors]
    y2 = [int(row["y"]) + int(row["height"]) for row in actors]
    return {
        "x": min(xs),
        "y": min(ys),
        "width": max(x2) - min(xs),
        "height": max(y2) - min(ys),
    }


def region_anchor(region: dict | None) -> str | None:
    if region is None:
        return None
    mid_x = int(region["x"]) + int(region["width"]) // 2
    if mid_x < 213:
        return "left"
    if mid_x < 426:
        return "center"
    return "right"


def scene_summary(rows: list[dict], family: str) -> dict:
    observed_entities = sorted({entity for row in rows for entity in row["entities"]})
    observed_states = sorted({row["frame_state"] for row in rows})
    observed_pose_labels = sorted({label for row in rows for label in row["pose_labels"]})
    key_frames = [row["frame_number"] for row in rows if row["frame_state"] != "background_only"]
    return {
        "scene_family": family,
        "observed_entities": observed_entities,
        "observed_frame_states": observed_states,
        "observed_pose_labels": observed_pose_labels,
        "key_frames": key_frames,
    }


def compile_semantic_truth(root: Path) -> dict:
    scenes = []
    for scene_dir in sorted(path for path in root.iterdir() if path.is_dir()):
        meta_dir = scene_dir / "frame-meta"
        if not meta_dir.is_dir():
            continue
        rows = []
        label = scene_dir.name
        for meta_path in sorted(meta_dir.glob("frame_*.json")):
            summary = summarize(meta_path)
            label = summary.get("scene_label") or label
            entities = sorted(summary.get("actor_summary", {}).keys())
            bmp_names = [row.get("bmp_name") for row in summary.get("actor_candidates", [])]
            family = scene_family(label)
            visible_unique_draws = summary.get("visible_unique_draws") or []
            ctx = context_labels(visible_unique_draws)
            pose = classify_pose_labels(bmp_names, entities, family)
            region = frame_region(summary)
            frame_state = classify_frame_state(entities, pose, family)
            rows.append(
                {
                    "frame_number": int(summary.get("frame_number", 0)),
                    "scene_label": label,
                    "scene_family": family,
                    "entities": entities,
                    "actor_count": int(summary.get("actor_candidate_draw_count", 0)),
                    "bmp_names": bmp_names,
                    "pose_labels": pose,
                    "context_labels": ctx,
                    "frame_state": frame_state,
                    "semantic_confidence": semantic_confidence(frame_state, int(summary.get("actor_candidate_draw_count", 0)), bmp_names),
                    "semantic_reasons": semantic_reasons(family, frame_state, entities, bmp_names, ctx),
                    "actor_region": region,
                    "region_anchor": region_anchor(region),
                }
            )
        scenes.append(
            {
                "scene_dir": scene_dir.name,
                "scene_label": label,
                "scene_family": scene_family(label),
                "scene_summary": scene_summary(rows, scene_family(label)),
                "rows": rows,
            }
        )
    return {"root": str(root.resolve()), "scenes": scenes}


def main() -> int:
    parser = argparse.ArgumentParser(description="Compile deterministic semantic truth from host frame metadata")
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--out-json", type=Path, required=True)
    args = parser.parse_args()

    payload = compile_semantic_truth(args.root)
    args.out_json.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
