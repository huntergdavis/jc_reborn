#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path


def load_semantic(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def rows_by_frame(scene: dict) -> dict[int, dict]:
    return {int(row["frame_number"]): row for row in scene.get("rows", [])}


def jaccard(a: set[str], b: set[str]) -> float:
    if not a and not b:
        return 1.0
    union = a | b
    if not union:
        return 0.0
    return len(a & b) / len(union)


def query_profile(scene: dict) -> dict:
    rows = list(scene.get("rows", []))
    active_rows = [row for row in rows if row.get("frame_state") != "background_only"]
    state_change_count = sum(1 for row in rows if row.get("state_changed"))
    frame_states = sorted({row.get("frame_state") for row in rows if row.get("frame_state")})
    subjects = sorted({row.get("primary_subject") for row in active_rows if row.get("primary_subject") not in (None, "none")})
    activities = sorted({label for row in rows for label in row.get("activity_labels", [])})
    contexts = sorted({label for row in rows for label in row.get("context_labels", [])})
    poses = sorted(
        {
            label
            for row in active_rows
            for label in row.get("pose_labels", [])
            if label not in ("no_actor_visible", "actor_visible")
        }
    )
    return {
        "frame_count": len(rows),
        "active_frame_count": len(active_rows),
        "state_change_count": state_change_count,
        "frame_states": frame_states,
        "subjects": subjects,
        "activities": activities,
        "contexts": contexts,
        "poses": poses,
    }


def summarize_match_evidence(query_scene: dict, match: dict) -> list[str]:
    profile = query_profile(query_scene)
    evidence: list[str] = []
    if match.get("exact_scene_signature"):
        evidence.append("exact_scene_signature")
    if match.get("family_match"):
        evidence.append("scene_family_match")
    if float(match.get("shared_active_frame_coverage", 0.0)) >= 1.0 and profile["active_frame_count"] > 0:
        evidence.append("full_active_frame_coverage")
    elif float(match.get("shared_active_frame_coverage", 0.0)) >= 0.5 and profile["active_frame_count"] > 0:
        evidence.append("partial_active_frame_coverage")
    if int(match.get("exact_active_state_matches", 0)) > 0:
        evidence.append("active_state_alignment")
    if int(match.get("exact_active_primary_subject_matches", 0)) > 0:
        evidence.append("active_subject_alignment")
    if int(match.get("exact_active_pose_matches", 0)) > 0:
        evidence.append("active_pose_alignment")
    if float(match.get("active_state_set_similarity", 0.0)) >= 0.95 and len(profile["frame_states"]) > 1:
        evidence.append("active_state_set_alignment")
    elif float(match.get("active_state_set_similarity", 0.0)) >= 0.5 and len(profile["frame_states"]) > 1:
        evidence.append("partial_active_state_set_alignment")
    if float(match.get("active_pose_set_similarity", 0.0)) >= 0.95 and profile["poses"]:
        evidence.append("active_pose_set_alignment")
    elif float(match.get("active_pose_set_similarity", 0.0)) >= 0.5 and profile["poses"]:
        evidence.append("partial_active_pose_set_alignment")
    if profile["state_change_count"] > 0:
        evidence.append("state_transition_evidence")
    if float(match.get("pose_similarity", 0.0)) >= 0.8 and profile["poses"]:
        evidence.append("pose_alignment")
    if float(match.get("activity_similarity", 0.0)) >= 0.95:
        evidence.append("activity_alignment")
    if float(match.get("token_similarity", 0.0)) >= 0.8:
        evidence.append("token_alignment")
    if float(match.get("trait_similarity", 0.0)) >= 0.5:
        evidence.append("trait_alignment")
    if profile["active_frame_count"] == 0:
        evidence.append("background_only_query")
    return evidence


def summarize_decision_context(query_scene: dict, best: dict | None, second: dict | None) -> dict:
    profile = query_profile(query_scene)
    best_score = float((best or {}).get("score", 0.0))
    second_score = float((second or {}).get("score", 0.0))
    return {
        "query_profile": profile,
        "best_evidence": summarize_match_evidence(query_scene, best or {}),
        "second_evidence": summarize_match_evidence(query_scene, second or {}) if second else [],
        "score_margin": round(best_score - second_score, 6),
        "score_ratio": round(best_score / second_score, 6) if second_score > 0.0 else None,
    }


def compare_scenes(query: dict, candidate: dict) -> dict:
    query_rows = rows_by_frame(query)
    cand_rows = rows_by_frame(candidate)
    shared_frames = sorted(set(query_rows) & set(cand_rows))
    query_frame_count = len(query_rows)
    query_active_frames = {
        frame_no
        for frame_no, row in query_rows.items()
        if row.get("frame_state") != "background_only"
    }
    query_active_states = {
        row.get("frame_state")
        for row in query_rows.values()
        if row.get("frame_state") and row.get("frame_state") != "background_only"
    }
    candidate_active_states = {
        row.get("frame_state")
        for row in cand_rows.values()
        if row.get("frame_state") and row.get("frame_state") != "background_only"
    }
    query_pose_set = {
        label
        for row in query_rows.values()
        for label in row.get("pose_labels", [])
        if label not in ("no_actor_visible", "actor_visible")
    }
    candidate_pose_set = {
        label
        for row in cand_rows.values()
        for label in row.get("pose_labels", [])
        if label not in ("no_actor_visible", "actor_visible")
    }

    exact_frame_signature_matches = 0
    exact_state_matches = 0
    exact_primary_subject_matches = 0
    exact_active_state_matches = 0
    exact_active_primary_subject_matches = 0
    exact_active_pose_matches = 0
    token_similarity_sum = 0.0
    activity_overlap_sum = 0.0
    pose_overlap_sum = 0.0

    for frame_no in shared_frames:
        q = query_rows[frame_no]
        c = cand_rows[frame_no]
        if q.get("frame_signature") == c.get("frame_signature"):
            exact_frame_signature_matches += 1
        if q.get("frame_state") == c.get("frame_state"):
            exact_state_matches += 1
        if q.get("primary_subject") == c.get("primary_subject"):
            exact_primary_subject_matches += 1
        token_similarity_sum += jaccard(
            set(q.get("identification_tokens", [])),
            set(c.get("identification_tokens", [])),
        )
        activity_overlap_sum += jaccard(
            set(q.get("activity_labels", [])),
            set(c.get("activity_labels", [])),
        )
        q_pose_labels = {
            label for label in q.get("pose_labels", []) if label not in ("no_actor_visible", "actor_visible")
        }
        c_pose_labels = {
            label for label in c.get("pose_labels", []) if label not in ("no_actor_visible", "actor_visible")
        }
        pose_overlap_sum += jaccard(q_pose_labels, c_pose_labels)
        if (
            frame_no in query_active_frames
            and c.get("frame_state") != "background_only"
        ):
            if q.get("frame_state") == c.get("frame_state"):
                exact_active_state_matches += 1
            if q.get("primary_subject") == c.get("primary_subject"):
                exact_active_primary_subject_matches += 1
            if q_pose_labels and q_pose_labels == c_pose_labels:
                exact_active_pose_matches += 1

    shared_count = len(shared_frames)
    shared_active_count = len(query_active_frames & set(cand_rows))
    background_only_query = not query_active_frames
    shared_frame_coverage = (shared_count / query_frame_count) if query_frame_count else 0.0
    shared_active_frame_coverage = (
        shared_active_count / len(query_active_frames)
        if query_active_frames else 0.0
    )
    token_similarity = (token_similarity_sum / shared_count) if shared_count else 0.0
    activity_similarity = (activity_overlap_sum / shared_count) if shared_count else 0.0
    pose_similarity = (pose_overlap_sum / shared_count) if shared_count else 0.0
    active_state_set_similarity = jaccard(query_active_states, candidate_active_states)
    active_pose_set_similarity = jaccard(query_pose_set, candidate_pose_set)

    exact_scene_signature = (
        (query.get("scene_summary") or {}).get("scene_signature")
        == (candidate.get("scene_summary") or {}).get("scene_signature")
    )

    family_match = query.get("scene_family") == candidate.get("scene_family")
    trait_similarity = jaccard(
        set((query.get("scene_summary") or {}).get("identification_traits", [])),
        set((candidate.get("scene_summary") or {}).get("identification_traits", [])),
    )

    score = 0.0
    score += 100.0 if exact_scene_signature else 0.0
    score += exact_frame_signature_matches * 10.0
    if background_only_query:
        score += exact_state_matches * 1.0
        score += token_similarity * 8.0
        score += activity_similarity * 3.0
        score += shared_frame_coverage * 8.0
    else:
        score += exact_state_matches * 3.0
        score += exact_primary_subject_matches * 2.0
        score += token_similarity * 20.0
        score += activity_similarity * 10.0
        score += shared_frame_coverage * 25.0
    score += shared_active_frame_coverage * 30.0
    score += exact_active_state_matches * 8.0
    score += exact_active_primary_subject_matches * 6.0
    score += exact_active_pose_matches * 6.0
    score += 5.0 if family_match else 0.0
    score += 0.0 if background_only_query else pose_similarity * 12.0
    score += trait_similarity * 10.0
    if query.get("scene_family") in (None, "", "unknown") and not background_only_query:
        score -= exact_primary_subject_matches * 1.0
        score -= exact_active_primary_subject_matches * 3.0
        score -= exact_active_pose_matches * 3.0
        score -= pose_similarity * 6.0
        if query_frame_count == len(query_active_frames):
            score -= 8.0
        if len(query_active_states) > 1:
            score -= (1.0 - active_state_set_similarity) * 24.0
        if len(query_pose_set) > 1:
            score -= (1.0 - active_pose_set_similarity) * 12.0

    return {
        "scene_label": candidate.get("scene_label"),
        "scene_family": candidate.get("scene_family"),
        "score": round(score, 6),
        "exact_scene_signature": exact_scene_signature,
        "family_match": family_match,
        "shared_frame_count": shared_count,
        "shared_frame_coverage": round(shared_frame_coverage, 6),
        "shared_active_frame_count": shared_active_count,
        "shared_active_frame_coverage": round(shared_active_frame_coverage, 6),
        "exact_frame_signature_matches": exact_frame_signature_matches,
        "exact_state_matches": exact_state_matches,
        "exact_primary_subject_matches": exact_primary_subject_matches,
        "exact_active_state_matches": exact_active_state_matches,
        "exact_active_primary_subject_matches": exact_active_primary_subject_matches,
        "exact_active_pose_matches": exact_active_pose_matches,
        "token_similarity": round(token_similarity, 6),
        "activity_similarity": round(activity_similarity, 6),
        "pose_similarity": round(pose_similarity, 6),
        "active_state_set_similarity": round(active_state_set_similarity, 6),
        "active_pose_set_similarity": round(active_pose_set_similarity, 6),
        "trait_similarity": round(trait_similarity, 6),
        "candidate_scene_signature": (candidate.get("scene_summary") or {}).get("scene_signature"),
    }


def identify_status(query_scene: dict, best: dict | None, second: dict | None) -> tuple[str, str]:
    if not best:
        return "unknown", "no candidates"
    profile = query_profile(query_scene)
    query_family = query_scene.get("scene_family")
    active_row_count = profile["active_frame_count"]
    state_change_count = profile["state_change_count"]
    if active_row_count == 0 and not best.get("exact_scene_signature"):
        return "unknown", "background-only query lacks actor evidence"
    if active_row_count > 0 and state_change_count == 0 and profile["frame_count"] <= 1 and not best.get("exact_scene_signature"):
        return "unknown", "single-frame active query lacks transition evidence"
    score = float(best.get("score", 0.0))
    second_score = float((second or {}).get("score", 0.0))
    margin = score - second_score
    ratio = (score / second_score) if second_score > 0.0 else None
    if best.get("exact_scene_signature"):
        return "identified", "exact scene signature match"
    if query_family in (None, "", "unknown"):
        if active_row_count < 1 or (active_row_count < 2 and state_change_count < 1):
            return "unknown", f"unknown-family query lacks semantic evidence active={active_row_count} changes={state_change_count}"
        if score >= 110.0 and margin >= 60.0 and ratio is not None and ratio >= 3.0:
            return "identified", f"strong unknown-family score {score:.3f} margin {margin:.3f}"
        if score >= 70.0 and margin >= 20.0:
            return "ambiguous", f"unknown-family partial match score {score:.3f} margin {margin:.3f}"
        return "unknown", f"unknown-family weak score {score:.3f} margin {margin:.3f}"
    if score >= 40.0 and margin >= 30.0 and (second_score <= 10.0 or (ratio is not None and ratio >= 4.0)):
        return "identified", f"strong separated score {score:.3f} margin {margin:.3f}"
    if score >= 80.0 and margin >= 25.0:
        return "identified", f"strong score {score:.3f} with margin {margin:.3f}"
    if score >= 50.0 and margin >= 10.0:
        return "ambiguous", f"partial match score {score:.3f} margin {margin:.3f}"
    return "unknown", f"weak score {score:.3f} margin {margin:.3f}"


def identify(database: dict, query: dict) -> dict:
    database_scenes = database.get("scenes", [])
    rows = []
    for query_scene in query.get("scenes", []):
        matches = [compare_scenes(query_scene, candidate) for candidate in database_scenes]
        matches.sort(key=lambda item: (-item["score"], item["scene_label"] or ""))
        best = matches[0] if matches else None
        second = matches[1] if len(matches) > 1 else None
        status, reason = identify_status(query_scene, best, second)
        decision_context = summarize_decision_context(query_scene, best, second)
        rows.append(
            {
                "query_scene_label": query_scene.get("scene_label"),
                "query_scene_family": query_scene.get("scene_family"),
                "query_scene_signature": (query_scene.get("scene_summary") or {}).get("scene_signature"),
                "identification_status": status,
                "identification_reason": reason,
                "score_margin": round(float(best.get("score", 0.0)) - float((second or {}).get("score", 0.0)), 6) if best else None,
                "decision_context": decision_context,
                "best_match": best,
                "second_match": second,
                "matches": matches,
            }
        )
    return {"rows": rows}


def main() -> int:
    parser = argparse.ArgumentParser(description="Identify host scenes by semantic truth signatures")
    parser.add_argument("--database-json", type=Path, required=True)
    parser.add_argument("--query-json", type=Path, required=True)
    parser.add_argument("--out-json", type=Path)
    args = parser.parse_args()

    report = identify(load_semantic(args.database_json), load_semantic(args.query_json))
    payload = json.dumps(report, indent=2) + "\n"
    if args.out_json:
        args.out_json.write_text(payload, encoding="utf-8")
    else:
        print(payload, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
