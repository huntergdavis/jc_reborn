#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
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


def primary_subject(entities: list[str], actor_count: int, frame_state: str) -> str:
    if actor_count == 0:
        return "none"
    if "johnny" in entities and "mary" not in entities and "suzy" not in entities:
        return "johnny"
    if "mary" in entities and "johnny" not in entities and "suzy" not in entities:
        return "mary"
    if "suzy" in entities and len(entities) == 1:
        return "suzy"
    if frame_state == "johnny_mary_pair":
        return "johnny_mary_pair"
    return "mixed"


def activity_labels(frame_state: str, pose_labels: list[str], context_labels: list[str], family: str) -> list[str]:
    labels: list[str] = []
    if frame_state == "background_only":
        labels.append("idle_background")
    if "johnny_fishing_pose" in pose_labels:
        labels.append("fishing_activity")
    elif frame_state == "johnny_only" and family == "fishing":
        labels.append("fishing_scene_presence")
    if "johnny_walking_pose" in pose_labels:
        labels.append("walking_activity")
    if "johnny_reading_pose" in pose_labels:
        labels.append("reading_activity")
    if "date_scene_pose" in pose_labels:
        labels.append("date_activity")
    if "breakup_scene_pose" in pose_labels:
        labels.append("breakup_activity")
    if family == "mary" and frame_state == "johnny_in_mary_scene":
        labels.append("mary_scene_johnny_presence")
    if "tree_visible" in context_labels:
        labels.append("tree_context")
    if "raft_visible" in context_labels:
        labels.append("raft_context")
    return labels


def primary_activity(activity_labels: list[str]) -> str:
    for preferred in (
        "fishing_activity",
        "date_activity",
        "breakup_activity",
        "walking_activity",
        "reading_activity",
        "mary_scene_johnny_presence",
        "fishing_scene_presence",
        "idle_background",
    ):
        if preferred in activity_labels:
            return preferred
    return activity_labels[0] if activity_labels else "unknown_activity"


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


def frame_signature(row: dict) -> str:
    payload = {
        "frame_state": row["frame_state"],
        "primary_subject": row["primary_subject"],
        "entities": row["entities"],
        "bmp_names": row["bmp_names"],
        "pose_labels": row["pose_labels"],
        "context_labels": row["context_labels"],
        "region_anchor": row["region_anchor"],
    }
    compact = json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(compact).hexdigest()[:16]


def identification_tokens(row: dict) -> list[str]:
    tokens = [
        f"family:{row['scene_family']}",
        f"state:{row['frame_state']}",
        f"subject:{row['primary_subject']}",
        f"activity:{row['primary_activity']}",
        f"anchor:{row['region_anchor'] or 'none'}",
    ]
    tokens.extend(f"entity:{entity}" for entity in row["entities"])
    tokens.extend(f"ctx:{label}" for label in row["context_labels"])
    return tokens


def annotate_transitions(rows: list[dict]) -> None:
    prev_state = None
    stable_run_length = 0
    for row in rows:
        current = row["frame_state"]
        if current == prev_state:
            stable_run_length += 1
            row["state_changed"] = False
        else:
            stable_run_length = 1
            row["state_changed"] = prev_state is not None
        row["stable_run_length"] = stable_run_length
        row["previous_frame_state"] = prev_state
        prev_state = current


def scene_summary(rows: list[dict], family: str) -> dict:
    observed_entities = sorted({entity for row in rows for entity in row["entities"]})
    observed_states = sorted({row["frame_state"] for row in rows})
    observed_pose_labels = sorted({label for row in rows for label in row["pose_labels"]})
    observed_activities = sorted({label for row in rows for label in row["activity_labels"]})
    key_frames = [row["frame_number"] for row in rows if row["frame_state"] != "background_only"]
    dominant_state = None
    if rows:
        counts: dict[str, int] = {}
        for row in rows:
            counts[row["frame_state"]] = counts.get(row["frame_state"], 0) + 1
        dominant_state = sorted(counts.items(), key=lambda item: (-item[1], item[0]))[0][0]
    transition_points = [
        {"frame_number": row["frame_number"], "from": row["previous_frame_state"], "to": row["frame_state"]}
        for row in rows if row.get("state_changed")
    ]
    dominant_activity = None
    if rows:
        counts: dict[str, int] = {}
        for row in rows:
            counts[row["primary_activity"]] = counts.get(row["primary_activity"], 0) + 1
        dominant_activity = sorted(counts.items(), key=lambda item: (-item[1], item[0]))[0][0]
    context_counts: dict[str, int] = {}
    for row in rows:
        for label in row["context_labels"]:
            context_counts[label] = context_counts.get(label, 0) + 1
    stable_context = sorted(label for label, count in context_counts.items() if count >= max(1, len(rows) // 2))
    identification_traits = sorted({
        f"family:{family}",
        f"dominant_state:{dominant_state or 'none'}",
        f"dominant_activity:{dominant_activity or 'none'}",
        *(f"context:{label}" for label in stable_context),
        *(f"entity:{entity}" for entity in observed_entities),
    })
    timeline_signature = " > ".join(
        f"{row['frame_number']}:{row['frame_state']}"
        for row in rows
    )
    scene_signature_payload = json.dumps(
        {
            "family": family,
            "timeline_signature": timeline_signature,
            "observed_entities": observed_entities,
            "observed_activities": observed_activities,
        },
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return {
        "scene_family": family,
        "observed_entities": observed_entities,
        "observed_frame_states": observed_states,
        "observed_pose_labels": observed_pose_labels,
        "observed_activity_labels": observed_activities,
        "key_frames": key_frames,
        "dominant_frame_state": dominant_state,
        "dominant_activity": dominant_activity,
        "stable_context_labels": stable_context,
        "transition_points": transition_points,
        "timeline_signature": timeline_signature,
        "unique_frame_signatures": [row["frame_signature"] for row in rows],
        "identification_traits": identification_traits,
        "scene_signature": hashlib.sha256(scene_signature_payload).hexdigest()[:20],
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
            row = {
                "frame_number": int(summary.get("frame_number", 0)),
                "scene_label": label,
                "scene_family": family,
                "entities": entities,
                "actor_count": int(summary.get("actor_candidate_draw_count", 0)),
                "bmp_names": bmp_names,
                "pose_labels": pose,
                "context_labels": ctx,
                "frame_state": frame_state,
                "primary_subject": primary_subject(entities, int(summary.get("actor_candidate_draw_count", 0)), frame_state),
                "activity_labels": activity_labels(frame_state, pose, ctx, family),
                "primary_activity": primary_activity(activity_labels(frame_state, pose, ctx, family)),
                "semantic_confidence": semantic_confidence(frame_state, int(summary.get("actor_candidate_draw_count", 0)), bmp_names),
                "semantic_reasons": semantic_reasons(family, frame_state, entities, bmp_names, ctx),
                "actor_region": region,
                "region_anchor": region_anchor(region),
            }
            row["frame_signature"] = frame_signature(row)
            row["identification_tokens"] = identification_tokens(row)
            rows.append(row)
        annotate_transitions(rows)
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
