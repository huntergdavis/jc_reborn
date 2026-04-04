#!/usr/bin/env python3
"""
Structured VLM analysis for JC regtest frames.

Primary target runtime: OpenVINO GenAI + an OpenVINO-converted visual language
model such as Qwen2.5-VL-3B-Instruct-ov-int4.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image

import vision_classifier as vc


SCREEN_TYPES = {"black", "title", "ocean", "island", "scene", "unknown"}
CHARACTER_NAMES = {"Johnny", "Mary", "Suzy", "visitor", "unknown"}
POSITIONS = {
    "left",
    "center",
    "right",
    "upper-left",
    "upper-center",
    "upper-right",
    "lower-left",
    "lower-center",
    "lower-right",
    "unknown",
}


@dataclass
class CropVariantSet:
    tmpdir: tempfile.TemporaryDirectory[str]
    items: list[tuple[str, Path]]

    def cleanup(self) -> None:
        self.tmpdir.cleanup()


def _require_openvino() -> tuple[Any, Any]:
    try:
        import openvino as ov
        import openvino_genai
    except ImportError as exc:
        raise SystemExit(
            "OpenVINO VLM runtime is not installed. "
            "Use .venv-vlm and install openvino-genai first."
        ) from exc
    return ov, openvino_genai


def find_project_root() -> Path:
    here = Path(__file__).resolve().parent
    root = here.parent
    if (root / "regtest-references").is_dir():
        return root
    return Path.cwd()


def resolve_reference_bank(bank_dir: Path | None) -> Path | None:
    if bank_dir is not None and (bank_dir / "features.npy").is_file() and (bank_dir / "metadata.json").is_file():
        return bank_dir

    root = find_project_root()
    manifest_path = root / "vision-artifacts" / "vision-reference-pipeline-current" / "pipeline-manifest.json"
    if manifest_path.is_file():
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            ref_bank = manifest.get("reference_bank", {})
            features_path = Path(ref_bank.get("features_npy", ""))
            metadata_path = Path(ref_bank.get("metadata_json", ""))
            if features_path.is_file() and metadata_path.is_file():
                return features_path.parent
        except (OSError, json.JSONDecodeError, TypeError, ValueError):
            pass

    fallback = root / ".claude" / "worktrees" / "vlm-classifier-20260330-wt" / "vision-artifacts" / "vision-reference-pipeline-current" / "reference-bank"
    if (fallback / "features.npy").is_file() and (fallback / "metadata.json").is_file():
        return fallback
    return None


def load_bank(bank_dir: Path | None) -> dict[str, Any] | None:
    bank_dir = resolve_reference_bank(bank_dir)
    if bank_dir is None:
        return None
    features = np.load(bank_dir / "features.npy")
    metadata = json.loads((bank_dir / "metadata.json").read_text(encoding="utf-8"))
    return {
        "features": features,
        "frames": metadata["frames"],
        "scene_offsets": metadata.get("scene_offsets", {}),
    }


def reference_hints_for_image(image_path: Path, bank: dict[str, Any] | None, topk: int) -> list[dict[str, Any]]:
    if bank is None:
        return []
    rgb = vc.safe_image_to_rgb(image_path)
    if rgb is None:
        return []
    metrics, feature = vc.compute_metrics(rgb, None)
    _ = metrics
    query = vc.normalize_features(feature.reshape(1, -1))[0]
    hits = vc.cosine_topk(query, bank["features"], k=topk)
    hints = []
    for idx, score in hits:
        frame = bank["frames"][idx]
        hints.append(
            {
                "scene_id": frame["scene_id"],
                "family": frame["family"],
                "frame": frame["frame"],
                "score": score,
                "action_summary": frame.get("labels", {}).get("action_summary"),
                "screen_type": frame.get("labels", {}).get("screen_type"),
            }
        )
    return hints


def scene_priors(scene_id: str | None) -> dict[str, Any]:
    family = vc.scene_family(scene_id) if scene_id else "UNKNOWN"
    priors = {
        "expected_characters": [],
        "likely_actions": [],
        "likely_regions": [],
        "appearance_notes": [],
    }
    if family == "FISHING":
        priors = {
            "expected_characters": ["Johnny"],
            "likely_actions": ["fishing", "standing with rod", "dockside standing"],
            "likely_regions": ["left shoreline", "dock edge", "raft edge", "center shoreline"],
            "appearance_notes": ["Johnny may appear as a small red-clothed figure", "the fishing rod can be more visible than the body"],
        }
    elif family == "MARY":
        priors = {
            "expected_characters": ["Mary"],
            "likely_actions": ["standing", "walking", "reclining", "beach activity"],
            "likely_regions": ["beach foreground", "shoreline", "house area"],
            "appearance_notes": ["Mary may be small and pale-colored against sand"],
        }
    elif family == "SUZY":
        priors = {
            "expected_characters": ["Suzy"],
            "likely_actions": ["walking", "standing", "beach activity"],
            "likely_regions": ["beach foreground", "shoreline", "house area"],
            "appearance_notes": ["Suzy may be small and partially obscured"],
        }
    elif family in {"ACTIVITY", "VISITOR", "BUILDING", "STAND", "WALKSTUF"}:
        priors = {
            "expected_characters": ["Johnny"],
            "likely_actions": ["standing", "walking", "activity-specific action"],
            "likely_regions": ["foreground shoreline", "dock", "house area", "center scene"],
            "appearance_notes": ["characters may be tiny low-resolution sprites"],
        }
    return priors


def neighbor_frame_context(scene_dir: Path | None, frame_name: str, radius: int = 2) -> list[str]:
    if scene_dir is None:
        return []
    frames = vc.sample_frame_paths(scene_dir)
    names = [p.name for p in frames]
    try:
        idx = names.index(frame_name)
    except ValueError:
        return []
    lo = max(0, idx - radius)
    hi = min(len(names), idx + radius + 1)
    return [names[i] for i in range(lo, hi) if i != idx]


def collect_frame_paths(scene_dir: Path) -> list[Path]:
    frames_dir = scene_dir / "frames"
    source_dir = frames_dir if frames_dir.is_dir() else scene_dir
    if not source_dir.is_dir():
        return []
    frames = sorted(
        [p for p in source_dir.iterdir() if p.suffix.lower() in {".bmp", ".png", ".jpg", ".jpeg"}],
        key=vc.frame_number,
    )
    return frames


def build_prompt(
    scene_id: str | None,
    frame_name: str,
    hints: list[dict[str, Any]],
    region_name: str = "full frame",
    priors: dict[str, Any] | None = None,
    neighbors: list[str] | None = None,
) -> str:
    scene_line = f"Expected scene: {scene_id}." if scene_id else "Expected scene: unknown."
    priors = priors or {"expected_characters": [], "likely_actions": [], "likely_regions": [], "appearance_notes": []}
    hint_lines = []
    if hints:
        hint_lines.append("Nearest reference matches:")
        for i, hint in enumerate(hints, start=1):
            hint_lines.append(
                f"{i}. scene={hint['scene_id']} family={hint['family']} frame={hint['frame']} "
                f"score={hint['score']:.3f} screen={hint.get('screen_type')} "
                f"summary={hint.get('action_summary')}"
            )
    else:
        hint_lines.append("Nearest reference matches: none.")
    hints_text = "\n".join(hint_lines)
    neighbors_text = ", ".join(neighbors or []) or "none"
    return f"""You are analyzing a frame from the game Johnny Castaway.

{scene_line}
Frame name: {frame_name}.
Viewed region: {region_name}.
Neighboring frames in the same scene: {neighbors_text}.
Scene priors:
- expected characters: {", ".join(priors["expected_characters"]) or "unknown"}
- likely actions: {", ".join(priors["likely_actions"]) or "unknown"}
- likely regions: {", ".join(priors["likely_regions"]) or "unknown"}
- appearance notes: {", ".join(priors["appearance_notes"]) or "none"}
{hints_text}

Describe only what is visually present in the frame. Do not guess beyond the image.
If identity is uncertain, say so explicitly.
Inspect carefully for tiny or partially visible characters, especially near shoreline, dock, raft, or center foreground.
If a person is small, distant, or partly obscured but still visually present, include them with lower confidence instead of omitting them.

Return strict JSON only with this schema:
{{
  "screen_type": "one of: black, title, ocean, island, scene, unknown",
  "summary": "one sentence, concrete and visual",
  "characters": [
    {{
      "name": "one of: Johnny, Mary, Suzy, visitor, unknown",
      "confidence": 0.0,
      "position": "one of: left, center, right, upper-left, upper-center, upper-right, lower-left, lower-center, lower-right, unknown",
      "action": "short action phrase"
    }}
  ],
  "objects": ["raft", "dock", "fishing rod", "house", "beach", "ocean"],
  "actions": ["short action phrase"],
  "confidence": 0.0,
  "notes": "short caveat if needed"
}}

Favor precise statements like:
- "Johnny is standing near the right edge of the dock."
- "The frame shows only ocean and shoreline; no clear character is visible."

Rules:
- Output valid JSON only.
- For enum-like fields, choose exactly one value, not a pipe-delimited string.
- If no person is clearly visible, return an empty `characters` list.
- First decide whether any human figure is visible, even if tiny.
- If a fishing rod, posture, or silhouette strongly implies a visible character, mention that with lower confidence.
"""


def build_verify_prompt(scene_id: str | None, frame_name: str, region_name: str, priors: dict[str, Any]) -> str:
    return f"""You are doing a second-pass verification on a tiny pixel-art game frame from Johnny Castaway.

Expected scene: {scene_id or "unknown"}.
Frame: {frame_name}.
Viewed region: {region_name}.
Expected characters: {", ".join(priors["expected_characters"]) or "unknown"}.
Likely regions: {", ".join(priors["likely_regions"]) or "unknown"}.
Appearance notes: {", ".join(priors["appearance_notes"]) or "none"}.

Question: Is there any small human figure visible in this region, even if tiny, partial, or low-confidence?

Return strict JSON only:
{{
  "person_visible": true,
  "name": "Johnny|Mary|Suzy|visitor|unknown",
  "position": "left|center|right|upper-left|upper-center|upper-right|lower-left|lower-center|lower-right|unknown",
  "action": "short action phrase",
  "confidence": 0.0,
  "reason": "short visual reason"
}}

Rules:
- If no person is visible, return `person_visible: false`, `name: unknown`, `action: none`.
- Use low confidence instead of false certainty.
"""


def build_locate_prompt(scene_id: str | None, frame_name: str, region_name: str, character_name: str) -> str:
    return f"""You are checking whether a specific tiny character is visible in a cropped Johnny Castaway frame region.

Expected scene: {scene_id or "unknown"}.
Frame: {frame_name}.
Viewed region: {region_name}.
Target character: {character_name}.

Return strict JSON only:
{{
  "visible": true,
  "confidence": 0.0,
  "reason": "short visual reason"
}}

Rules:
- Use `visible: true` only if the target character is actually visible in this crop.
- If uncertain, lower confidence instead of guessing.
"""


def build_state_prompt(scene_id: str | None, frame_name: str, priors: dict[str, Any], region_name: str = "full frame") -> str:
    return f"""You are classifying the visual state of a Johnny Castaway frame.

Expected scene: {scene_id or "unknown"}.
Frame: {frame_name}.
Viewed region: {region_name}.
Expected characters: {", ".join(priors["expected_characters"]) or "unknown"}.
Likely actions: {", ".join(priors["likely_actions"]) or "unknown"}.
Appearance notes: {", ".join(priors["appearance_notes"]) or "none"}.

Choose the best overall frame state.

Return strict JSON only:
{{
  "screen_type": "black|title|ocean|island|scene|unknown",
  "sprites_visible": true,
  "confidence": 0.0,
  "reason": "short visual reason"
}}

Definitions:
- black: nearly blank or black frame
- title: title screen, logo, menu, or static splash
- ocean: mostly ocean/background with little or no visible character
- island: island or shoreline background dominates, with props but no clearly visible active character
- scene: any live scene with a visible actor, or a clearly active pose/action such as fishing, walking, standing with rod, or interacting with props

Rules:
- Prefer `scene` if a visible person or active fishing/standing pose is present.
- Prefer `scene` if `sprites_visible` would be true.
- Prefer `title` only for obvious title/logo/menu art.
- Output valid JSON only.
"""


def build_compare_prompt(
    scene_id: str | None,
    reference_name: str,
    query_name: str,
    priors: dict[str, Any],
    reference_state: dict[str, Any] | None = None,
    query_state: dict[str, Any] | None = None,
) -> str:
    reference_state = reference_state or {}
    query_state = query_state or {}
    return f"""You are comparing two frames from the game Johnny Castaway.

Expected scene: {scene_id or "unknown"}.
Image A is the reference frame: {reference_name}.
Image B is the query frame to compare against the reference: {query_name}.

Scene priors:
- expected characters: {", ".join(priors["expected_characters"]) or "unknown"}
- likely actions: {", ".join(priors["likely_actions"]) or "unknown"}
- likely regions: {", ".join(priors["likely_regions"]) or "unknown"}
- appearance notes: {", ".join(priors["appearance_notes"]) or "none"}
Independent state estimates:
- reference state: {reference_state.get("screen_type", "unknown")} (sprites_visible={reference_state.get("sprites_visible", "unknown")}, confidence={reference_state.get("confidence", 0.0)})
- query state: {query_state.get("screen_type", "unknown")} (sprites_visible={query_state.get("sprites_visible", "unknown")}, confidence={query_state.get("confidence", 0.0)})

Compare the images carefully. Focus on:
- black screen vs title screen vs ocean/background vs live scene
- missing characters
- extra or duplicated characters
- shifted character position
- changed action or pose
- missing objects like rod, raft, dock, house

Return strict JSON only:
{{
  "reference_screen_type": "black|title|ocean|island|scene|unknown",
  "query_screen_type": "black|title|ocean|island|scene|unknown",
  "same_scene_state": true,
  "summary": "short concrete comparison summary",
  "missing_from_query": ["short item"],
  "extra_in_query": ["short item"],
  "position_changes": ["short item"],
  "character_diff": {{
    "reference_characters": ["Johnny"],
    "query_characters": ["Johnny"],
    "missing_characters": [],
    "duplicated_characters": []
  }},
  "confidence": 0.0
}}

Rules:
- If the query is missing an expected tiny character visible in the reference, say so explicitly.
- If you are uncertain, keep confidence lower instead of inventing detail.
"""


def extract_json(text: str) -> dict[str, Any]:
    text = text.strip()
    if text.startswith("{") and text.endswith("}"):
        return json.loads(text)
    match = re.search(r"\{.*\}", text, flags=re.S)
    if not match:
        raise ValueError(f"No JSON object found in model output: {text[:300]!r}")
    raw = match.group(0)
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        repaired = re.sub(r",\s*([}\]])", r"\1", raw)
        repaired = re.sub(r"(?<!\\)\n", " ", repaired)
        repaired = re.sub(r"\s+", " ", repaired)
        return json.loads(repaired)


def canonical_screen_type(value: Any) -> str:
    text = str(value or "").strip().lower()
    if text in SCREEN_TYPES:
        return text
    if text in {"background", "bg"}:
        return "island"
    if "title" in text or "menu" in text or "logo" in text:
        return "title"
    if "ocean" in text or "water" in text or "sea" in text:
        return "ocean"
    if "black" in text or "blank" in text:
        return "black"
    if "island" in text or "shore" in text or "beach" in text:
        return "island"
    if "scene" in text or "sprite" in text or "character" in text:
        return "scene"
    return "unknown"


def normalize_heuristic_screen_type(value: str) -> str:
    screen_type = canonical_screen_type(value)
    if screen_type == "unknown" and value == "background":
        return "ocean"
    return "ocean" if value == "background" else screen_type


def canonical_character_name(value: Any) -> str:
    text = str(value or "").strip()
    if text in CHARACTER_NAMES:
        return text
    lowered = text.lower()
    for name in ("Johnny", "Mary", "Suzy", "visitor", "unknown"):
        if name.lower() in lowered:
            return name
    return "unknown"


def canonical_position(value: Any) -> str:
    text = str(value or "").strip().lower()
    if text in POSITIONS:
        return text
    aliases = {
        "upper left": "upper-left",
        "upper center": "upper-center",
        "upper right": "upper-right",
        "lower left": "lower-left",
        "lower center": "lower-center",
        "lower right": "lower-right",
        "mid": "center",
        "middle": "center",
    }
    return aliases.get(text, "unknown")


def clamp_confidence(value: Any, default: float = 0.0) -> float:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return default
    return max(0.0, min(1.0, number))


def sanitize_state_payload(payload: dict[str, Any]) -> dict[str, Any]:
    return {
        "screen_type": canonical_screen_type(payload.get("screen_type")),
        "sprites_visible": bool(payload.get("sprites_visible", False)),
        "confidence": clamp_confidence(payload.get("confidence"), default=0.0),
        "reason": str(payload.get("reason") or "").strip(),
        "_meta": payload.get("_meta", {}),
    }


def heuristic_state_labels(image_path: Path, scene_id: str | None) -> dict[str, Any] | None:
    rgb = vc.safe_image_to_rgb(image_path)
    if rgb is None:
        return None
    metrics, _ = vc.compute_metrics(rgb, None)
    labels = vc.classify_labels(scene_id or "UNKNOWN-0", metrics)
    return {
        "screen_type": normalize_heuristic_screen_type(labels.get("screen_type", "unknown")),
        "sprites_visible": bool(labels.get("sprites_visible", False)),
        "raw_screen_type": labels.get("screen_type", "unknown"),
        "action_summary": labels.get("action_summary", ""),
    }


def apply_state_guardrails(state: dict[str, Any], heuristic: dict[str, Any] | None) -> dict[str, Any]:
    state = sanitize_state_payload(state)
    if heuristic is None:
        return state

    state["_meta"]["heuristic_state"] = heuristic
    heuristic_screen = heuristic["screen_type"]
    heuristic_sprites = heuristic["sprites_visible"]

    if heuristic_screen == "black" and state["screen_type"] != "black":
        state["screen_type"] = "black"
        state["sprites_visible"] = False
        state["confidence"] = min(state["confidence"], 0.25)
        state["reason"] = f"{state['reason']} Heuristic override: frame is black-like.".strip()
        return state

    if state["screen_type"] == "title" and heuristic_screen in {"black", "ocean"}:
        state["screen_type"] = heuristic_screen
        state["sprites_visible"] = False
        state["confidence"] = min(state["confidence"], 0.25)
        state["reason"] = f"{state['reason']} Heuristic override: rejected title classification.".strip()
        return state

    if state["screen_type"] == "scene" and state["sprites_visible"] and heuristic_screen == "black":
        state["screen_type"] = "black"
        state["sprites_visible"] = False
        state["confidence"] = min(state["confidence"], 0.25)
        state["reason"] = f"{state['reason']} Heuristic override: no visible scene content.".strip()
        return state

    if state["screen_type"] == "scene" and not state["sprites_visible"] and not heuristic_sprites and heuristic_screen in {"ocean", "black"}:
        state["screen_type"] = heuristic_screen
        state["confidence"] = min(state["confidence"], 0.25)
        state["reason"] = f"{state['reason']} Heuristic override: no visible actor evidence.".strip()
        return state

    return state


def clean_string_list(values: Any) -> list[str]:
    if not isinstance(values, list):
        return []
    cleaned: list[str] = []
    seen: set[str] = set()
    for value in values:
        text = str(value or "").strip()
        if not text:
            continue
        key = text.lower()
        if key in seen:
            continue
        seen.add(key)
        cleaned.append(text)
    return cleaned


def sanitize_characters(values: Any) -> list[dict[str, Any]]:
    if not isinstance(values, list):
        return []
    cleaned = []
    for item in values:
        if not isinstance(item, dict):
            continue
        name = canonical_character_name(item.get("name"))
        position = canonical_position(item.get("position"))
        action = str(item.get("action") or "unknown").strip() or "unknown"
        confidence = clamp_confidence(item.get("confidence"), default=0.0)
        if name == "unknown" and confidence <= 0.0 and action == "unknown":
            continue
        cleaned.append(
            {
                "name": name,
                "confidence": confidence,
                "position": position,
                "action": action,
            }
        )
    return cleaned


def sanitize_analysis_payload(payload: dict[str, Any], state: dict[str, Any] | None = None) -> dict[str, Any]:
    cleaned = dict(payload)
    cleaned["screen_type"] = canonical_screen_type(cleaned.get("screen_type"))
    cleaned["summary"] = str(cleaned.get("summary") or "").strip()
    cleaned["characters"] = sanitize_characters(cleaned.get("characters"))
    cleaned["objects"] = clean_string_list(cleaned.get("objects"))
    cleaned["actions"] = clean_string_list(cleaned.get("actions"))
    cleaned["confidence"] = clamp_confidence(cleaned.get("confidence"), default=0.0)
    cleaned["notes"] = str(cleaned.get("notes") or "").strip()
    if cleaned["characters"]:
        cleaned["screen_type"] = "scene"
    elif state:
        state_screen = canonical_screen_type(state.get("screen_type"))
        if state_screen != "unknown":
            cleaned["screen_type"] = state_screen
    return cleaned


def sanitize_compare_payload(payload: dict[str, Any], reference_state: dict[str, Any], query_state: dict[str, Any]) -> dict[str, Any]:
    cleaned = dict(payload)
    cleaned["reference_screen_type"] = canonical_screen_type(
        cleaned.get("reference_screen_type") or reference_state.get("screen_type")
    )
    cleaned["query_screen_type"] = canonical_screen_type(
        cleaned.get("query_screen_type") or query_state.get("screen_type")
    )
    cleaned["same_scene_state"] = bool(cleaned.get("same_scene_state", False))
    cleaned["summary"] = str(cleaned.get("summary") or "").strip()
    cleaned["missing_from_query"] = clean_string_list(cleaned.get("missing_from_query"))
    cleaned["extra_in_query"] = clean_string_list(cleaned.get("extra_in_query"))
    cleaned["position_changes"] = clean_string_list(cleaned.get("position_changes"))
    diff = cleaned.get("character_diff")
    if not isinstance(diff, dict):
        diff = {}
    cleaned["character_diff"] = {
        "reference_characters": clean_string_list(diff.get("reference_characters")),
        "query_characters": clean_string_list(diff.get("query_characters")),
        "missing_characters": clean_string_list(diff.get("missing_characters")),
        "duplicated_characters": clean_string_list(diff.get("duplicated_characters")),
    }
    cleaned["confidence"] = clamp_confidence(cleaned.get("confidence"), default=0.0)
    return cleaned


def load_image_tensor(image_path: Path, image_size: int) -> tuple[Any, Any]:
    ov, _ = _require_openvino()
    image = Image.open(image_path).convert("RGB")
    image = image.resize((image_size, image_size), Image.Resampling.BILINEAR)
    arr = np.array(image, dtype=np.uint8).reshape(1, image.size[1], image.size[0], 3)
    return image, ov.Tensor(arr)


def crop_variants(image_path: Path, image_size: int) -> CropVariantSet:
    image = Image.open(image_path).convert("RGB")
    w, h = image.size
    regions = {
        "full frame": (0, 0, w, h),
        "center crop": (w // 6, h // 6, w * 5 // 6, h * 5 // 6),
        "lower-center crop": (w // 5, h // 3, w * 4 // 5, h),
        "right-shore crop": (w // 2, h // 4, w, h),
        "left-shore crop": (0, h // 4, w // 2, h),
    }
    tmpdir_obj = tempfile.TemporaryDirectory(prefix="vision_vlm_crops_")
    tmpdir = Path(tmpdir_obj.name)
    out = []
    for name, box in regions.items():
        cropped = image.crop(box).resize((image_size, image_size), Image.Resampling.BILINEAR)
        path = tmpdir / f"{name.replace(' ', '_')}.png"
        cropped.save(path)
        out.append((name, path))
    return CropVariantSet(tmpdir=tmpdir_obj, items=out)


def region_bucket(region_name: str) -> str:
    if "left" in region_name:
        return "left"
    if "right" in region_name:
        return "right"
    return "center"


def run_vlm(
    pipe: Any,
    model_dir: Path,
    image_path: Path,
    prompt: str,
    device: str,
    max_new_tokens: int,
    image_size: int,
) -> dict[str, Any]:
    _, image_tensor = load_image_tensor(image_path, image_size)
    result = pipe.generate(prompt, image=image_tensor, max_new_tokens=max_new_tokens)
    text = result.texts[0]
    payload = extract_json(text)
    payload["_meta"] = {
        "model_dir": str(model_dir),
        "device": device,
        "image": str(image_path),
        "image_size": image_size,
        "raw_response": text,
    }
    return payload


def classify_frame_state(
    pipe: Any,
    model_dir: Path,
    image_path: Path,
    scene_id: str | None,
    priors: dict[str, Any],
    device: str,
    max_new_tokens: int,
    image_size: int,
    region_name: str = "full frame",
) -> dict[str, Any]:
    prompt = build_state_prompt(scene_id, image_path.name, priors, region_name)
    payload = run_vlm(pipe, model_dir, image_path, prompt, device, max_new_tokens, image_size)
    state = {
        "screen_type": payload.get("screen_type", "unknown"),
        "sprites_visible": bool(payload.get("sprites_visible", False)),
        "confidence": float(payload.get("confidence", 0.0)),
        "reason": payload.get("reason", ""),
        "_meta": payload.get("_meta", {}),
    }
    heuristic = heuristic_state_labels(image_path, scene_id)
    return apply_state_guardrails(state, heuristic)


def refine_character_positions(
    pipe: Any,
    model_dir: Path,
    image_path: Path,
    scene_id: str | None,
    payload: dict[str, Any],
    device: str,
    max_new_tokens: int,
    image_size: int,
) -> None:
    chars = payload.get("characters") or []
    if not chars:
        return
    crop_set = crop_variants(image_path, image_size)
    try:
        crops = crop_set.items[1:]
        for char in chars:
            current_pos = char.get("position", "unknown")
            current_conf = float(char.get("confidence", 0.0))
            if current_pos not in {"center", "unknown"} and current_conf >= 0.6:
                continue
            target = char.get("name", "unknown")
            best_bucket = None
            best_score = -1.0
            evidence = []
            for region_name, crop_path in crops:
                prompt = build_locate_prompt(scene_id, image_path.name, region_name, target)
                locate = run_vlm(pipe, model_dir, crop_path, prompt, device, min(max_new_tokens, 120), image_size)
                score = float(locate.get("confidence", 0.0)) if locate.get("visible") else 0.0
                bucket = region_bucket(region_name)
                evidence.append({"region": region_name, "bucket": bucket, "score": score})
                if score > best_score:
                    best_score = score
                    best_bucket = bucket
            if best_bucket and best_score >= max(0.45, current_conf):
                char["position"] = best_bucket
                payload.setdefault("_meta", {})["position_refinement"] = evidence
    finally:
        crop_set.cleanup()


def analyze_image(
    model_dir: Path,
    image_path: Path,
    out_json: Path,
    *,
    bank_dir: Path | None,
    scene_id: str | None,
    scene_dir: Path | None,
    topk: int,
    device: str,
    max_new_tokens: int,
    image_size: int,
    pipe: Any | None = None,
) -> None:
    bank = load_bank(bank_dir)
    hints = reference_hints_for_image(image_path, bank, topk=topk)
    priors = scene_priors(scene_id)
    neighbors = neighbor_frame_context(scene_dir, image_path.name)
    if pipe is None:
        _, openvino_genai = _require_openvino()
        pipe = openvino_genai.VLMPipeline(str(model_dir), device)
    state = classify_frame_state(pipe, model_dir, image_path, scene_id, priors, device, min(max_new_tokens, 120), image_size)
    prompt = build_prompt(scene_id, image_path.name, hints, "full frame", priors, neighbors)
    payload = run_vlm(pipe, model_dir, image_path, prompt, device, max_new_tokens, image_size)
    payload = sanitize_analysis_payload(payload, state)
    payload["_meta"]["scene_id"] = scene_id
    payload["_meta"]["nearest_reference_hints"] = hints
    payload["_meta"]["scene_priors"] = priors
    payload["_meta"]["neighbor_frames"] = neighbors
    payload["_meta"]["pass"] = "full frame"
    payload["_meta"]["frame_state"] = state
    if state.get("screen_type") in {"black", "title", "ocean", "island"}:
        payload["screen_type"] = state["screen_type"]

    if not payload.get("characters"):
        crop_set = crop_variants(image_path, image_size)
        try:
            for region_name, crop_path in crop_set.items[1:]:
                crop_prompt = build_prompt(scene_id, image_path.name, hints, region_name, priors, neighbors)
                crop_payload = run_vlm(pipe, model_dir, crop_path, crop_prompt, device, max_new_tokens, image_size)
                crop_payload = sanitize_analysis_payload(crop_payload)
                crop_chars = crop_payload.get("characters") or []
                if crop_chars:
                    payload["_meta"]["fallback_region_used"] = region_name
                    payload["_meta"]["fallback_raw_response"] = crop_payload["_meta"]["raw_response"]
                    payload["characters"] = crop_chars
                    if not payload.get("actions") and crop_payload.get("actions"):
                        payload["actions"] = crop_payload["actions"]
                    if payload.get("summary"):
                        payload["summary"] = f"{payload['summary']} Fallback region {region_name} suggests: {crop_payload.get('summary', '')}".strip()
                    break
                verify_prompt = build_verify_prompt(scene_id, image_path.name, region_name, priors)
                verify_payload = run_vlm(pipe, model_dir, crop_path, verify_prompt, device, min(max_new_tokens, 160), image_size)
                if verify_payload.get("person_visible") is True:
                    payload["_meta"]["verification_region_used"] = region_name
                    payload["_meta"]["verification_raw_response"] = verify_payload["_meta"]["raw_response"]
                    payload["characters"] = [
                        {
                            "name": canonical_character_name(verify_payload.get("name")),
                            "confidence": clamp_confidence(verify_payload.get("confidence"), default=0.0),
                            "position": canonical_position(verify_payload.get("position")),
                            "action": str(verify_payload.get("action", "unknown")).strip() or "unknown",
                        }
                    ]
                    if not payload.get("actions"):
                        action = verify_payload.get("action")
                        payload["actions"] = [] if not action or action == "none" else [str(action).strip()]
                    if payload.get("summary"):
                        payload["summary"] = f"{payload['summary']} Verification pass on {region_name} suggests a small visible person."
                    break
        finally:
            crop_set.cleanup()
    if payload.get("characters"):
        payload["screen_type"] = "scene"
        refine_character_positions(pipe, model_dir, image_path, scene_id, payload, device, max_new_tokens, image_size)
    out_json.parent.mkdir(parents=True, exist_ok=True)
    out_json.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def classify_state_image(
    model_dir: Path,
    image_path: Path,
    out_json: Path,
    *,
    scene_id: str | None,
    device: str,
    max_new_tokens: int,
    image_size: int,
) -> None:
    _, openvino_genai = _require_openvino()
    pipe = openvino_genai.VLMPipeline(str(model_dir), device)
    priors = scene_priors(scene_id)
    state = classify_frame_state(pipe, model_dir, image_path, scene_id, priors, device, max_new_tokens, image_size)
    state["_meta"]["scene_id"] = scene_id
    state["_meta"]["pass"] = "state_only"
    out_json.parent.mkdir(parents=True, exist_ok=True)
    out_json.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")


def classify_state_dir(
    model_dir: Path,
    scene_dir: Path,
    outdir: Path,
    *,
    scene_id: str | None,
    samples: int,
    device: str,
    max_new_tokens: int,
    image_size: int,
) -> None:
    frames = collect_frame_paths(scene_dir)
    selected = vc.evenly_sample(frames, min(samples, len(frames)))
    _, openvino_genai = _require_openvino()
    pipe = openvino_genai.VLMPipeline(str(model_dir), device)
    rows = []
    for i, frame_path in enumerate(selected, start=1):
        print(f"[{i}/{len(selected)}] {frame_path.name}", file=sys.stderr, flush=True)
        start_time = vc.now_millis() if hasattr(vc, "now_millis") else None
        priors = scene_priors(scene_id or scene_dir.name)
        state = classify_frame_state(
            pipe,
            model_dir,
            frame_path,
            scene_id or scene_dir.name,
            priors,
            device,
            max_new_tokens,
            image_size,
        )
        if start_time is not None:
            state["_meta"]["elapsed_ms"] = vc.now_millis() - start_time
        state["_meta"]["scene_id"] = scene_id or scene_dir.name
        state["_meta"]["pass"] = "state_only"
        out_json = outdir / "frames" / f"{frame_path.stem}.json"
        out_json.parent.mkdir(parents=True, exist_ok=True)
        out_json.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
        rows.append(
            {
                "frame": frame_path.name,
                "relative_frame_path": os.path.relpath(frame_path, outdir),
                "analysis": state,
            }
        )
    summary = {
        "scene_id": scene_id or scene_dir.name,
        "frame_count": len(rows),
        "frames": [
            {
                "frame": row["frame"],
                "screen_type": row["analysis"].get("screen_type"),
                "sprites_visible": row["analysis"].get("sprites_visible"),
                "confidence": row["analysis"].get("confidence"),
                "analysis_path": f"frames/{Path(row['frame']).stem}.json",
            }
            for row in rows
        ],
    }
    outdir.mkdir(parents=True, exist_ok=True)
    (outdir / "state-summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    render_scene_review(outdir / "review.html", scene_id or scene_dir.name, rows)


def render_scene_review(out_html: Path, scene_id: str, rows: list[dict[str, Any]]) -> None:
    parts = []
    for row in rows:
        data = row["analysis"]
        chars = data.get("characters") or []
        char_text = "<br>".join(
            f"{vc.html_escape(c.get('name'))} @ {vc.html_escape(c.get('position'))}: {vc.html_escape(c.get('action'))} ({c.get('confidence', 0):.2f})"
            for c in chars
        ) or "none"
        objects = ", ".join(data.get("objects") or []) or "-"
        actions = ", ".join(data.get("actions") or []) or "-"
        parts.append(
            f"""
            <tr>
              <td><img src="{vc.html_escape(row['relative_frame_path'])}" style="width:320px; image-rendering: pixelated;"></td>
              <td>
                <div><strong>{vc.html_escape(row['frame'])}</strong></div>
                <div>{vc.html_escape(data.get('summary'))}</div>
                <div>screen: {vc.html_escape(data.get('screen_type'))}</div>
                <div>confidence: {float(data.get('confidence', 0.0)):.2f}</div>
              </td>
              <td>{char_text}</td>
              <td>{vc.html_escape(objects)}</td>
              <td>{vc.html_escape(actions)}</td>
            </tr>
            """
        )
    html = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>{vc.html_escape(scene_id)} VLM Review</title>
  <style>
    body {{ font-family: sans-serif; margin: 24px; }}
    table {{ border-collapse: collapse; width: 100%; }}
    td, th {{ border: 1px solid #ccc; vertical-align: top; padding: 8px; }}
  </style>
</head>
<body>
  <h1>{vc.html_escape(scene_id)} VLM Review</h1>
  <table>
    <thead>
      <tr><th>Frame</th><th>Summary</th><th>Characters</th><th>Objects</th><th>Actions</th></tr>
    </thead>
    <tbody>{''.join(parts)}</tbody>
  </table>
</body>
</html>
"""
    out_html.write_text(html, encoding="utf-8")


def render_compare_review(out_html: Path, scene_id: str, rows: list[dict[str, Any]]) -> None:
    parts = []
    for row in rows:
        data = row["analysis"]
        missing = ", ".join(data.get("missing_from_query") or []) or "-"
        extra = ", ".join(data.get("extra_in_query") or []) or "-"
        moves = ", ".join(data.get("position_changes") or []) or "-"
        parts.append(
            f"""
            <tr>
              <td><img src="{vc.html_escape(row['relative_reference_path'])}" style="width:280px; image-rendering: pixelated;"></td>
              <td><img src="{vc.html_escape(row['relative_query_path'])}" style="width:280px; image-rendering: pixelated;"></td>
              <td>
                <div><strong>{vc.html_escape(row['reference_frame'])} vs {vc.html_escape(row['query_frame'])}</strong></div>
                <div>{vc.html_escape(data.get('summary'))}</div>
                <div>ref: {vc.html_escape(data.get('reference_screen_type'))}</div>
                <div>query: {vc.html_escape(data.get('query_screen_type'))}</div>
                <div>same state: {vc.html_escape(data.get('same_scene_state'))}</div>
                <div>confidence: {float(data.get('confidence', 0.0)):.2f}</div>
              </td>
              <td>{vc.html_escape(missing)}</td>
              <td>{vc.html_escape(extra)}</td>
              <td>{vc.html_escape(moves)}</td>
            </tr>
            """
        )
    html = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>{vc.html_escape(scene_id)} VLM Compare Review</title>
  <style>
    body {{ font-family: sans-serif; margin: 24px; }}
    table {{ border-collapse: collapse; width: 100%; }}
    td, th {{ border: 1px solid #ccc; vertical-align: top; padding: 8px; }}
  </style>
</head>
<body>
  <h1>{vc.html_escape(scene_id)} VLM Compare Review</h1>
  <table>
    <thead>
      <tr><th>Reference</th><th>Query</th><th>Summary</th><th>Missing</th><th>Extra</th><th>Position Changes</th></tr>
    </thead>
    <tbody>{''.join(parts)}</tbody>
  </table>
</body>
</html>
"""
    out_html.write_text(html, encoding="utf-8")


def analyze_scene_dir(
    model_dir: Path,
    scene_dir: Path,
    outdir: Path,
    *,
    bank_dir: Path | None,
    scene_id: str | None,
    samples: int,
    topk: int,
    device: str,
    max_new_tokens: int,
    image_size: int,
) -> None:
    frames = collect_frame_paths(scene_dir)
    selected = vc.evenly_sample(frames, min(samples, len(frames)))
    _, openvino_genai = _require_openvino()
    pipe = openvino_genai.VLMPipeline(str(model_dir), device)
    rows = []
    for i, frame_path in enumerate(selected, start=1):
        print(f"[{i}/{len(selected)}] {frame_path.name}", file=sys.stderr, flush=True)
        frame_out = outdir / "frames" / f"{frame_path.stem}.json"
        analyze_image(
            model_dir,
            frame_path,
            frame_out,
            bank_dir=bank_dir,
            scene_id=scene_id or scene_dir.name,
            scene_dir=scene_dir,
            topk=topk,
            device=device,
            max_new_tokens=max_new_tokens,
            image_size=image_size,
            pipe=pipe,
        )
        analysis = json.loads(frame_out.read_text(encoding="utf-8"))
        rows.append(
            {
                "frame": frame_path.name,
                "relative_frame_path": os.path.relpath(frame_path, outdir),
                "analysis": analysis,
            }
        )
    outdir.mkdir(parents=True, exist_ok=True)
    summary = {
        "scene_id": scene_id or scene_dir.name,
        "frame_count": len(rows),
        "frames": [{"frame": row["frame"], "analysis_path": f"frames/{Path(row['frame']).stem}.json"} for row in rows],
    }
    (outdir / "vlm-analysis.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    render_scene_review(outdir / "review.html", scene_id or scene_dir.name, rows)


def compare_images(
    model_dir: Path,
    reference_image: Path,
    query_image: Path,
    out_json: Path,
    *,
    scene_id: str | None,
    device: str,
    max_new_tokens: int,
    image_size: int,
    pipe: Any | None = None,
) -> None:
    priors = scene_priors(scene_id)
    if pipe is None:
        _, openvino_genai = _require_openvino()
        pipe = openvino_genai.VLMPipeline(str(model_dir), device)
    ref_crops = crop_variants(reference_image, image_size)
    query_crops = crop_variants(query_image, image_size)
    try:
        ref_tmp = ref_crops.items[0][1]
        query_tmp = query_crops.items[0][1]
        ref_image, ref_tensor = load_image_tensor(ref_tmp, image_size)
        query_image_resized, query_tensor = load_image_tensor(query_tmp, image_size)
        _ = ref_image, query_image_resized
        reference_state = classify_frame_state(pipe, model_dir, ref_tmp, scene_id, priors, device, min(max_new_tokens, 120), image_size)
        query_state = classify_frame_state(pipe, model_dir, query_tmp, scene_id, priors, device, min(max_new_tokens, 120), image_size)
        prompt = build_compare_prompt(scene_id, reference_image.name, query_image.name, priors, reference_state, query_state)
        result = pipe.generate(prompt, images=[ref_tensor, query_tensor], max_new_tokens=max_new_tokens)
        text = result.texts[0]
        payload = extract_json(text)
        payload = sanitize_compare_payload(payload, reference_state, query_state)
        payload["_meta"] = {
            "model_dir": str(model_dir),
            "device": device,
            "reference_image": str(reference_image),
            "query_image": str(query_image),
            "image_size": image_size,
            "scene_id": scene_id,
            "scene_priors": priors,
            "reference_state": reference_state,
            "query_state": query_state,
            "raw_response": text,
        }
        out_json.parent.mkdir(parents=True, exist_ok=True)
        out_json.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    finally:
        ref_crops.cleanup()
        query_crops.cleanup()


def _is_scene_content_frame(path: Path, yellow_threshold: float = 1.0) -> bool:
    """Quick check if a frame has island/scene content (yellow sand pixels)."""
    try:
        img = Image.open(path)
        px = np.array(img)
        if px.ndim < 3 or px.shape[2] < 3:
            return False
        r, g, b = px[:, :, 0], px[:, :, 1], px[:, :, 2]
        yellow = np.sum((r > 200) & (g > 200) & (b < 120))
        total = px.shape[0] * px.shape[1]
        return (yellow * 100.0 / total) > yellow_threshold
    except Exception:
        return False


def _filter_scene_content_frames(frames: list[Path], yellow_threshold: float = 1.0) -> list[Path]:
    """Filter to only frames with visible scene content (island/sprites)."""
    return [f for f in frames if _is_scene_content_frame(f, yellow_threshold)]


def compare_scene_dirs(
    model_dir: Path,
    reference_scene_dir: Path,
    query_scene_dir: Path,
    outdir: Path,
    *,
    scene_id: str | None,
    samples: int,
    device: str,
    max_new_tokens: int,
    image_size: int,
) -> None:
    ref_frames = collect_frame_paths(reference_scene_dir)
    query_frames = collect_frame_paths(query_scene_dir)

    # Filter to scene-content frames only (skip black/BIOS/title/ocean).
    # Reference: skip early idle frames, prefer mid-scene action.
    # PS1: skip BIOS boot + title + ocean (frames before ~2400).
    ref_scene = _filter_scene_content_frames(ref_frames, yellow_threshold=1.5)
    query_scene = _filter_scene_content_frames(query_frames, yellow_threshold=1.5)

    # Fall back to all frames if filtering removed everything
    if not ref_scene:
        ref_scene = ref_frames
        print(f"  WARN: no scene-content frames in reference, using all {len(ref_frames)}", file=sys.stderr)
    if not query_scene:
        query_scene = query_frames
        print(f"  WARN: no scene-content frames in PS1 query, using all {len(query_frames)}", file=sys.stderr)

    # For reference: skip first 20% (usually idle island) to get action frames
    if len(ref_scene) > 5:
        skip = max(1, len(ref_scene) // 5)
        ref_scene = ref_scene[skip:]

    count = min(len(ref_scene), len(query_scene), samples)
    ref_sel = vc.evenly_sample(ref_scene, count)
    query_sel = vc.evenly_sample(query_scene, count)
    outdir.mkdir(parents=True, exist_ok=True)
    _, openvino_genai = _require_openvino()
    pipe = openvino_genai.VLMPipeline(str(model_dir), device)
    rows = []
    for i, (ref_frame, query_frame) in enumerate(zip(ref_sel, query_sel), start=1):
        print(f"[{i}/{count}] {ref_frame.name} vs {query_frame.name}", file=sys.stderr, flush=True)
        out_json = outdir / "frames" / f"{ref_frame.stem}__vs__{query_frame.stem}.json"
        compare_images(
            model_dir,
            ref_frame,
            query_frame,
            out_json,
            scene_id=scene_id,
            device=device,
            max_new_tokens=max_new_tokens,
            image_size=image_size,
            pipe=pipe,
        )
        analysis = json.loads(out_json.read_text(encoding="utf-8"))
        rows.append(
            {
                "reference_frame": ref_frame.name,
                "query_frame": query_frame.name,
                "relative_reference_path": os.path.relpath(ref_frame, outdir),
                "relative_query_path": os.path.relpath(query_frame, outdir),
                "analysis": analysis,
            }
        )
    summary = {
        "scene_id": scene_id or reference_scene_dir.name,
        "pair_count": len(rows),
    }
    (outdir / "vlm-compare-analysis.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    render_compare_review(outdir / "review.html", scene_id or reference_scene_dir.name, rows)


def parse_args() -> argparse.Namespace:
    root = find_project_root()
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)

    p1 = sub.add_parser("analyze-image", help="Analyze a single image with a real VLM")
    p1.add_argument("--model-dir", type=Path, required=True)
    p1.add_argument("--image", type=Path, required=True)
    p1.add_argument("--out-json", type=Path, required=True)
    p1.add_argument("--bank-dir", type=Path)
    p1.add_argument("--scene-id", type=str)
    p1.add_argument("--topk", type=int, default=3)
    p1.add_argument("--device", type=str, default="CPU")
    p1.add_argument("--max-new-tokens", type=int, default=220)
    p1.add_argument("--image-size", type=int, default=256)

    p2 = sub.add_parser("classify-state", help="Run a single state-classification pass on one image")
    p2.add_argument("--model-dir", type=Path, required=True)
    p2.add_argument("--image", type=Path, required=True)
    p2.add_argument("--out-json", type=Path, required=True)
    p2.add_argument("--scene-id", type=str)
    p2.add_argument("--device", type=str, default="CPU")
    p2.add_argument("--max-new-tokens", type=int, default=80)
    p2.add_argument("--image-size", type=int, default=128)

    p3 = sub.add_parser("classify-state-dir", help="Run state-only classification on sampled frames from a scene directory")
    p3.add_argument("--model-dir", type=Path, required=True)
    p3.add_argument("--scene-dir", type=Path, required=True)
    p3.add_argument("--outdir", type=Path, required=True)
    p3.add_argument("--scene-id", type=str)
    p3.add_argument("--samples", type=int, default=6)
    p3.add_argument("--device", type=str, default="CPU")
    p3.add_argument("--max-new-tokens", type=int, default=80)
    p3.add_argument("--image-size", type=int, default=128)

    p4 = sub.add_parser("analyze-scene-dir", help="Analyze sampled frames from a scene directory")
    p4.add_argument("--model-dir", type=Path, required=True)
    p4.add_argument("--scene-dir", type=Path, required=True)
    p4.add_argument("--outdir", type=Path, required=True)
    p4.add_argument("--bank-dir", type=Path, default=root / "vision-artifacts" / "vision-reference-pipeline-current" / "reference-bank")
    p4.add_argument("--scene-id", type=str)
    p4.add_argument("--samples", type=int, default=8)
    p4.add_argument("--topk", type=int, default=3)
    p4.add_argument("--device", type=str, default="CPU")
    p4.add_argument("--max-new-tokens", type=int, default=220)
    p4.add_argument("--image-size", type=int, default=256)

    p5 = sub.add_parser("compare-images", help="Compare a reference image and a query image with the VLM")
    p5.add_argument("--model-dir", type=Path, required=True)
    p5.add_argument("--reference-image", type=Path, required=True)
    p5.add_argument("--query-image", type=Path, required=True)
    p5.add_argument("--out-json", type=Path, required=True)
    p5.add_argument("--scene-id", type=str)
    p5.add_argument("--device", type=str, default="CPU")
    p5.add_argument("--max-new-tokens", type=int, default=260)
    p5.add_argument("--image-size", type=int, default=256)

    p6 = sub.add_parser("compare-scene-dirs", help="Compare sampled frames from a reference scene and query scene")
    p6.add_argument("--model-dir", type=Path, required=True)
    p6.add_argument("--reference-scene-dir", type=Path, required=True)
    p6.add_argument("--query-scene-dir", type=Path, required=True)
    p6.add_argument("--outdir", type=Path, required=True)
    p6.add_argument("--scene-id", type=str)
    p6.add_argument("--samples", type=int, default=6)
    p6.add_argument("--device", type=str, default="CPU")
    p6.add_argument("--max-new-tokens", type=int, default=260)
    p6.add_argument("--image-size", type=int, default=256)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.cmd == "analyze-image":
        analyze_image(
            args.model_dir,
            args.image,
            args.out_json,
            bank_dir=args.bank_dir,
            scene_id=args.scene_id,
            scene_dir=args.image.parent.parent if args.image.parent.name == "frames" else None,
            topk=args.topk,
            device=args.device,
            max_new_tokens=args.max_new_tokens,
            image_size=args.image_size,
        )
    elif args.cmd == "classify-state":
        classify_state_image(
            args.model_dir,
            args.image,
            args.out_json,
            scene_id=args.scene_id,
            device=args.device,
            max_new_tokens=args.max_new_tokens,
            image_size=args.image_size,
        )
    elif args.cmd == "classify-state-dir":
        classify_state_dir(
            args.model_dir,
            args.scene_dir,
            args.outdir,
            scene_id=args.scene_id,
            samples=args.samples,
            device=args.device,
            max_new_tokens=args.max_new_tokens,
            image_size=args.image_size,
        )
    elif args.cmd == "analyze-scene-dir":
        analyze_scene_dir(
            args.model_dir,
            args.scene_dir,
            args.outdir,
            bank_dir=args.bank_dir,
            scene_id=args.scene_id,
            samples=args.samples,
            topk=args.topk,
            device=args.device,
            max_new_tokens=args.max_new_tokens,
            image_size=args.image_size,
        )
    elif args.cmd == "compare-images":
        compare_images(
            args.model_dir,
            args.reference_image,
            args.query_image,
            args.out_json,
            scene_id=args.scene_id,
            device=args.device,
            max_new_tokens=args.max_new_tokens,
            image_size=args.image_size,
        )
    elif args.cmd == "compare-scene-dirs":
        compare_scene_dirs(
            args.model_dir,
            args.reference_scene_dir,
            args.query_scene_dir,
            args.outdir,
            scene_id=args.scene_id,
            samples=args.samples,
            device=args.device,
            max_new_tokens=args.max_new_tokens,
            image_size=args.image_size,
        )
    else:
        raise SystemExit(f"Unknown command: {args.cmd}")


if __name__ == "__main__":
    main()
