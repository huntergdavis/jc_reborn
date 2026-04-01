#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path


def load_semantic(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def rows_by_frame(scene: dict) -> dict[int, dict]:
    return {int(row["frame_number"]): row for row in scene.get("rows", [])}


def context_change_points(rows: dict[int, dict]) -> set[int]:
    points: set[int] = set()
    previous: frozenset[str] | None = None
    for frame_no in sorted(rows):
        current = frozenset(rows[frame_no].get("context_labels", []))
        if previous is not None and current != previous:
            points.add(frame_no)
        previous = current
    return points


def background_provenance_profile(rows: dict[int, dict]) -> dict:
    ordered = sorted(rows)
    if not ordered:
        return {
            "pre_active_contexts": set(),
            "post_active_contexts": set(),
            "pre_active_count": 0,
            "post_active_count": 0,
        }

    active_frames = [
        frame_no
        for frame_no in ordered
        if rows[frame_no].get("frame_state") != "background_only"
    ]
    if not active_frames:
        background_contexts = {
            label
            for frame_no in ordered
            for label in rows[frame_no].get("context_labels", [])
        }
        return {
            "pre_active_contexts": background_contexts,
            "post_active_contexts": set(),
            "pre_active_count": len(ordered),
            "post_active_count": 0,
        }

    first_active = active_frames[0]
    last_active = active_frames[-1]
    pre_active_frames = [frame_no for frame_no in ordered if frame_no < first_active]
    post_active_frames = [frame_no for frame_no in ordered if frame_no > last_active]
    return {
        "pre_active_contexts": {
            label
            for frame_no in pre_active_frames
            for label in rows[frame_no].get("context_labels", [])
        },
        "post_active_contexts": {
            label
            for frame_no in post_active_frames
            for label in rows[frame_no].get("context_labels", [])
        },
        "pre_active_count": len(pre_active_frames),
        "post_active_count": len(post_active_frames),
    }


def query_contamination_profile(rows: dict[int, dict]) -> dict:
    ordered = sorted(rows)
    background_rows = [rows[frame_no] for frame_no in ordered if rows[frame_no].get("frame_state") == "background_only"]
    active_rows = [rows[frame_no] for frame_no in ordered if rows[frame_no].get("frame_state") != "background_only"]
    provenance = background_provenance_profile(rows)
    background_contexts = {
        label
        for row in background_rows
        for label in row.get("context_labels", [])
    }
    active_contexts = {
        label
        for row in active_rows
        for label in row.get("context_labels", [])
    }
    pre_contexts = set(provenance["pre_active_contexts"])
    post_contexts = set(provenance["post_active_contexts"])
    pre_post_similarity = jaccard(pre_contexts, post_contexts)
    background_active_similarity = jaccard(background_contexts, active_contexts)
    active_context_novelty = 1.0
    if active_contexts:
        active_context_novelty = 1.0 - (len(active_contexts & background_contexts) / max(1, len(active_contexts)))
    active_island_risk = 0.0
    if len(active_rows) == 1 and len(background_rows) >= 1:
        active_island_risk += (1.0 - background_active_similarity) * 0.5
        if provenance["pre_active_count"] > 0 and provenance["post_active_count"] == 0:
            active_island_risk += 0.35
        if provenance["pre_active_count"] == 0 and provenance["post_active_count"] > 0:
            active_island_risk += 0.2
    contamination_risk = 0.0
    if len(active_rows) == 1 and len(ordered) > 1:
        contamination_risk += 0.45
    if provenance["pre_active_count"] > 0 and provenance["post_active_count"] > 0:
        contamination_risk += 0.15
    if background_rows and active_rows:
        contamination_risk += (1.0 - background_active_similarity) * 0.2
    if pre_contexts and post_contexts:
        contamination_risk += (1.0 - pre_post_similarity) * 0.2
    return {
        "background_frame_count": len(background_rows),
        "active_frame_count": len(active_rows),
        "pre_active_count": provenance["pre_active_count"],
        "post_active_count": provenance["post_active_count"],
        "background_context_count": len(background_contexts),
        "background_active_context_similarity": round(background_active_similarity, 6),
        "active_context_novelty": round(active_context_novelty, 6),
        "pre_post_background_similarity": round(pre_post_similarity, 6),
        "active_island_risk": round(min(1.0, active_island_risk), 6),
        "contamination_risk": round(min(1.0, contamination_risk), 6),
    }


def subject_timeline_profile(rows: dict[int, dict]) -> dict:
    ordered = sorted(rows)
    if not ordered:
        return {
            "subjects": set(),
            "first_active_frame": None,
            "last_active_frame": None,
            "active_span": 0,
            "background_recovery_count": 0,
            "has_background_recovery": False,
            "active_run_lengths": {},
        }

    active_subjects = [
        (frame_no, rows[frame_no].get("primary_subject"))
        for frame_no in ordered
        if rows[frame_no].get("frame_state") != "background_only"
        and rows[frame_no].get("primary_subject") not in (None, "none")
    ]
    run_lengths: dict[str, int] = {}
    current_subject = None
    current_length = 0
    for frame_no in ordered:
        row = rows[frame_no]
        subject = row.get("primary_subject")
        if row.get("frame_state") == "background_only" or subject in (None, "none"):
            if current_subject is not None:
                run_lengths[current_subject] = max(run_lengths.get(current_subject, 0), current_length)
            current_subject = None
            current_length = 0
            continue
        if subject == current_subject:
            current_length += 1
        else:
            if current_subject is not None:
                run_lengths[current_subject] = max(run_lengths.get(current_subject, 0), current_length)
            current_subject = subject
            current_length = 1
    if current_subject is not None:
        run_lengths[current_subject] = max(run_lengths.get(current_subject, 0), current_length)

    first_active = active_subjects[0][0] if active_subjects else None
    last_active = active_subjects[-1][0] if active_subjects else None
    active_span = 0 if not active_subjects else (last_active - first_active)
    background_recovery_count = 0
    if active_subjects:
        background_recovery_count = sum(
            1
            for frame_no in ordered
            if frame_no > last_active and rows[frame_no].get("frame_state") == "background_only"
        )
    return {
        "subjects": {subject for _, subject in active_subjects},
        "first_active_frame": first_active,
        "last_active_frame": last_active,
        "active_span": active_span,
        "background_recovery_count": background_recovery_count,
        "has_background_recovery": background_recovery_count > 0,
        "active_run_lengths": run_lengths,
    }


def phase_sequence_profile(rows: dict[int, dict]) -> list[str]:
    ordered = sorted(rows)
    phases: list[str] = []
    last = None
    for frame_no in ordered:
        row = rows[frame_no]
        phase = "actor" if row.get("frame_state") != "background_only" else "background"
        if phase != last:
            phases.append(phase)
            last = phase
    return phases


def state_sequence_profile(rows: dict[int, dict]) -> list[str]:
    ordered = sorted(rows)
    sequence: list[str] = []
    last = None
    for frame_no in ordered:
        state = rows[frame_no].get("frame_state")
        if state != last:
            sequence.append(state)
            last = state
    return sequence


def state_transition_pairs(rows: dict[int, dict]) -> list[str]:
    sequence = state_sequence_profile(rows)
    return [f"{left}->{right}" for left, right in zip(sequence, sequence[1:])]


def timed_state_transition_pairs(rows: dict[int, dict]) -> list[str]:
    ordered = sorted(rows)
    if not ordered:
        return []
    max_frame = max(ordered) or 1
    pairs: list[str] = []
    last_state = None
    for frame_no in ordered:
        state = rows[frame_no].get("frame_state")
        if state != last_state:
            if last_state is not None:
                ratio = frame_no / max_frame
                bucket = "early" if ratio < (1.0 / 3.0) else "mid" if ratio < (2.0 / 3.0) else "late"
                pairs.append(f"{last_state}->{state}@{bucket}")
            last_state = state
    return pairs


def subject_activity_sequence(rows: dict[int, dict]) -> list[str]:
    ordered = sorted(rows)
    sequence: list[str] = []
    last = None
    for frame_no in ordered:
        row = rows[frame_no]
        if row.get("frame_state") == "background_only":
            key = "none:background"
        else:
            subject = row.get("primary_subject") or "unknown"
            activities = ",".join(sorted(row.get("activity_labels", []))) or "none"
            key = f"{subject}:{activities}"
        if key != last:
            sequence.append(key)
            last = key
    return sequence


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
    contamination = query_contamination_profile(rows_by_frame(scene))
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
    active_state_variety = sorted(
        {
            row.get("frame_state")
            for row in active_rows
            if row.get("frame_state")
        }
    )
    return {
        "frame_count": len(rows),
        "active_frame_count": len(active_rows),
        "active_frame_ratio": round((len(active_rows) / len(rows)) if rows else 0.0, 6),
        "state_change_count": state_change_count,
        "frame_states": frame_states,
        "subjects": subjects,
        "activities": activities,
        "contexts": contexts,
        "poses": poses,
        "active_state_variety": active_state_variety,
        "active_state_variety_count": len(active_state_variety),
        "contamination_risk": contamination["contamination_risk"],
        "active_island_risk": contamination["active_island_risk"],
        "background_frame_count": contamination["background_frame_count"],
        "background_active_context_similarity": contamination["background_active_context_similarity"],
        "active_context_novelty": contamination["active_context_novelty"],
        "pre_post_background_similarity": contamination["pre_post_background_similarity"],
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
    if float(match.get("transition_similarity", 0.0)) >= 0.95 and profile["state_change_count"] > 0:
        evidence.append("transition_alignment")
    elif float(match.get("transition_similarity", 0.0)) >= 0.5 and profile["state_change_count"] > 0:
        evidence.append("partial_transition_alignment")
    if int(match.get("exact_context_matches", 0)) > 0:
        evidence.append("context_timeline_overlap")
    if float(match.get("pose_similarity", 0.0)) >= 0.8 and profile["poses"]:
        evidence.append("pose_alignment")
    if float(match.get("activity_similarity", 0.0)) >= 0.95:
        evidence.append("activity_alignment")
    if float(match.get("token_similarity", 0.0)) >= 0.8:
        evidence.append("token_alignment")
    if float(match.get("context_set_similarity", 0.0)) >= 0.95 and profile["contexts"]:
        evidence.append("context_alignment")
    elif float(match.get("context_set_similarity", 0.0)) >= 0.5 and profile["contexts"]:
        evidence.append("partial_context_alignment")
    if float(match.get("context_transition_similarity", 0.0)) >= 0.95 and profile["contexts"]:
        evidence.append("context_transition_alignment")
    elif float(match.get("context_transition_similarity", 0.0)) >= 0.5 and profile["contexts"]:
        evidence.append("partial_context_transition_alignment")
    if float(match.get("background_provenance_similarity", 0.0)) >= 0.95 and profile["active_frame_count"] > 0:
        evidence.append("background_provenance_alignment")
    elif float(match.get("background_provenance_similarity", 0.0)) >= 0.5 and profile["active_frame_count"] > 0:
        evidence.append("partial_background_provenance_alignment")
    if float(match.get("subject_set_similarity", 0.0)) >= 0.95 and profile["subjects"]:
        evidence.append("subject_alignment")
    elif float(match.get("subject_set_similarity", 0.0)) >= 0.5 and profile["subjects"]:
        evidence.append("partial_subject_alignment")
    if float(match.get("first_active_timing_similarity", 0.0)) >= 0.95 and profile["active_frame_count"] > 0:
        evidence.append("subject_entry_timing_alignment")
    elif float(match.get("first_active_timing_similarity", 0.0)) >= 0.5 and profile["active_frame_count"] > 0:
        evidence.append("partial_subject_entry_timing_alignment")
    if float(match.get("last_active_timing_similarity", 0.0)) >= 0.95 and profile["active_frame_count"] > 0:
        evidence.append("subject_exit_timing_alignment")
    elif float(match.get("last_active_timing_similarity", 0.0)) >= 0.5 and profile["active_frame_count"] > 0:
        evidence.append("partial_subject_exit_timing_alignment")
    if float(match.get("subject_persistence_similarity", 0.0)) >= 0.95 and profile["subjects"]:
        evidence.append("subject_persistence_alignment")
    elif float(match.get("subject_persistence_similarity", 0.0)) >= 0.5 and profile["subjects"]:
        evidence.append("partial_subject_persistence_alignment")
    if float(match.get("background_recovery_similarity", 0.0)) >= 0.95 and profile["active_frame_count"] > 0:
        evidence.append("background_recovery_alignment")
    elif float(match.get("background_recovery_similarity", 0.0)) >= 0.5 and profile["active_frame_count"] > 0:
        evidence.append("partial_background_recovery_alignment")
    if float(match.get("phase_sequence_similarity", 0.0)) >= 0.95 and profile["frame_count"] > 0:
        evidence.append("phase_sequence_alignment")
    elif float(match.get("phase_sequence_similarity", 0.0)) >= 0.5 and profile["frame_count"] > 0:
        evidence.append("partial_phase_sequence_alignment")
    if float(match.get("phase_count_similarity", 0.0)) >= 0.95 and profile["frame_count"] > 0:
        evidence.append("phase_count_alignment")
    elif float(match.get("phase_count_similarity", 0.0)) >= 0.5 and profile["frame_count"] > 0:
        evidence.append("partial_phase_count_alignment")
    if float(match.get("state_sequence_similarity", 0.0)) >= 0.95 and profile["frame_count"] > 0:
        evidence.append("state_sequence_alignment")
    elif float(match.get("state_sequence_similarity", 0.0)) >= 0.5 and profile["frame_count"] > 0:
        evidence.append("partial_state_sequence_alignment")
    if float(match.get("state_count_similarity", 0.0)) >= 0.95 and profile["frame_count"] > 0:
        evidence.append("state_count_alignment")
    elif float(match.get("state_count_similarity", 0.0)) >= 0.5 and profile["frame_count"] > 0:
        evidence.append("partial_state_count_alignment")
    if float(match.get("transition_pair_similarity", 0.0)) >= 0.95 and profile["state_change_count"] > 0:
        evidence.append("transition_pair_alignment")
    elif float(match.get("transition_pair_similarity", 0.0)) >= 0.5 and profile["state_change_count"] > 0:
        evidence.append("partial_transition_pair_alignment")
    if float(match.get("transition_pair_count_similarity", 0.0)) >= 0.95 and profile["state_change_count"] > 0:
        evidence.append("transition_pair_count_alignment")
    elif float(match.get("transition_pair_count_similarity", 0.0)) >= 0.5 and profile["state_change_count"] > 0:
        evidence.append("partial_transition_pair_count_alignment")
    if float(match.get("timed_transition_pair_similarity", 0.0)) >= 0.95 and profile["state_change_count"] > 0:
        evidence.append("timed_transition_pair_alignment")
    elif float(match.get("timed_transition_pair_similarity", 0.0)) >= 0.5 and profile["state_change_count"] > 0:
        evidence.append("partial_timed_transition_pair_alignment")
    if float(match.get("timed_transition_pair_count_similarity", 0.0)) >= 0.95 and profile["state_change_count"] > 0:
        evidence.append("timed_transition_pair_count_alignment")
    elif float(match.get("timed_transition_pair_count_similarity", 0.0)) >= 0.5 and profile["state_change_count"] > 0:
        evidence.append("partial_timed_transition_pair_count_alignment")
    if float(match.get("subject_activity_sequence_similarity", 0.0)) >= 0.95 and profile["active_frame_count"] > 0:
        evidence.append("subject_activity_sequence_alignment")
    elif float(match.get("subject_activity_sequence_similarity", 0.0)) >= 0.5 and profile["active_frame_count"] > 0:
        evidence.append("partial_subject_activity_sequence_alignment")
    if float(match.get("subject_activity_count_similarity", 0.0)) >= 0.95 and profile["active_frame_count"] > 0:
        evidence.append("subject_activity_count_alignment")
    elif float(match.get("subject_activity_count_similarity", 0.0)) >= 0.5 and profile["active_frame_count"] > 0:
        evidence.append("partial_subject_activity_count_alignment")
    if float(match.get("borrowed_background_risk", 0.0)) >= 0.8:
        evidence.append("borrowed_background_detected")
    elif float(match.get("borrowed_background_risk", 0.0)) >= 0.4:
        evidence.append("partial_borrowed_background_risk")
    if float(match.get("borrowed_background_mismatch", 0.0)) >= 0.5:
        evidence.append("borrowed_background_mismatch")
    elif float(match.get("borrowed_background_mismatch", 0.0)) >= 0.25:
        evidence.append("partial_borrowed_background_mismatch")
    if float(match.get("sparse_active_evidence_penalty", 0.0)) >= 12.0:
        evidence.append("sparse_active_evidence")
    elif float(match.get("sparse_active_evidence_penalty", 0.0)) >= 6.0:
        evidence.append("partial_sparse_active_evidence")
    if float(match.get("fragmented_active_coverage_penalty", 0.0)) >= 12.0:
        evidence.append("fragmented_active_coverage")
    elif float(match.get("fragmented_active_coverage_penalty", 0.0)) >= 6.0:
        evidence.append("partial_fragmented_active_coverage")
    if float(match.get("active_semantic_diversity_penalty", 0.0)) >= 12.0:
        evidence.append("active_semantic_diversity_conflict")
    elif float(match.get("active_semantic_diversity_penalty", 0.0)) >= 6.0:
        evidence.append("partial_active_semantic_diversity_conflict")
    if float(match.get("active_island_penalty", 0.0)) >= 12.0:
        evidence.append("late_active_island_conflict")
    elif float(match.get("active_island_penalty", 0.0)) >= 6.0:
        evidence.append("partial_late_active_island_conflict")
    if float(match.get("borrowed_background_context_penalty", 0.0)) >= 10.0:
        evidence.append("borrowed_background_context_conflict")
    elif float(match.get("borrowed_background_context_penalty", 0.0)) >= 5.0:
        evidence.append("partial_borrowed_background_context_conflict")
    if float(match.get("blended_active_narrative_penalty", 0.0)) >= 12.0:
        evidence.append("blended_active_narrative_conflict")
    elif float(match.get("blended_active_narrative_penalty", 0.0)) >= 6.0:
        evidence.append("partial_blended_active_narrative_conflict")
    if float(match.get("single_active_alignment_penalty", 0.0)) >= 10.0:
        evidence.append("single_active_alignment_overweight")
    elif float(match.get("single_active_alignment_penalty", 0.0)) >= 5.0:
        evidence.append("partial_single_active_alignment_overweight")
    if float(match.get("low_novelty_multi_active_penalty", 0.0)) >= 10.0:
        evidence.append("low_novelty_multi_active_conflict")
    elif float(match.get("low_novelty_multi_active_penalty", 0.0)) >= 5.0:
        evidence.append("partial_low_novelty_multi_active_conflict")
    if float(match.get("late_active_island_context_penalty", 0.0)) >= 8.0:
        evidence.append("late_active_island_context_conflict")
    elif float(match.get("late_active_island_context_penalty", 0.0)) >= 4.0:
        evidence.append("partial_late_active_island_context_conflict")
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
        "best_borrowed_background_risk": round(float((best or {}).get("borrowed_background_risk", 0.0)), 6),
        "best_borrowed_background_mismatch": round(float((best or {}).get("borrowed_background_mismatch", 0.0)), 6),
        "second_borrowed_background_risk": round(float((second or {}).get("borrowed_background_risk", 0.0)), 6) if second else None,
        "second_borrowed_background_mismatch": round(float((second or {}).get("borrowed_background_mismatch", 0.0)), 6) if second else None,
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
    query_transition_points = {
        frame_no for frame_no, row in query_rows.items() if row.get("state_changed")
    }
    candidate_active_states = {
        row.get("frame_state")
        for row in cand_rows.values()
        if row.get("frame_state") and row.get("frame_state") != "background_only"
    }
    candidate_transition_points = {
        frame_no for frame_no, row in cand_rows.items() if row.get("state_changed")
    }
    query_pose_set = {
        label
        for row in query_rows.values()
        for label in row.get("pose_labels", [])
        if label not in ("no_actor_visible", "actor_visible")
    }
    query_context_set = {
        label
        for row in query_rows.values()
        for label in row.get("context_labels", [])
    }
    candidate_pose_set = {
        label
        for row in cand_rows.values()
        for label in row.get("pose_labels", [])
        if label not in ("no_actor_visible", "actor_visible")
    }
    candidate_context_set = {
        label
        for row in cand_rows.values()
        for label in row.get("context_labels", [])
    }
    query_context_change_points = context_change_points(query_rows)
    candidate_context_change_points = context_change_points(cand_rows)
    query_background_provenance = background_provenance_profile(query_rows)
    candidate_background_provenance = background_provenance_profile(cand_rows)
    query_subject_profile = subject_timeline_profile(query_rows)
    candidate_subject_profile = subject_timeline_profile(cand_rows)
    query_phase_sequence = phase_sequence_profile(query_rows)
    candidate_phase_sequence = phase_sequence_profile(cand_rows)
    query_state_sequence = state_sequence_profile(query_rows)
    candidate_state_sequence = state_sequence_profile(cand_rows)
    query_transition_pairs = state_transition_pairs(query_rows)
    candidate_transition_pairs = state_transition_pairs(cand_rows)
    query_timed_transition_pairs = timed_state_transition_pairs(query_rows)
    candidate_timed_transition_pairs = timed_state_transition_pairs(cand_rows)
    query_subject_activity_sequence = subject_activity_sequence(query_rows)
    candidate_subject_activity_sequence = subject_activity_sequence(cand_rows)

    exact_frame_signature_matches = 0
    exact_state_matches = 0
    exact_context_matches = 0
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
        if set(q.get("context_labels", [])) == set(c.get("context_labels", [])):
            exact_context_matches += 1
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
    has_active_alignment = shared_active_count > 0
    token_similarity = (token_similarity_sum / shared_count) if shared_count else 0.0
    activity_similarity = (activity_overlap_sum / shared_count) if shared_count else 0.0
    pose_similarity = (pose_overlap_sum / shared_count) if shared_count else 0.0
    active_state_set_similarity = jaccard(query_active_states, candidate_active_states)
    active_pose_set_similarity = jaccard(query_pose_set, candidate_pose_set)
    context_set_similarity = jaccard(query_context_set, candidate_context_set)
    transition_similarity = jaccard(query_transition_points, candidate_transition_points)
    context_transition_similarity = jaccard(query_context_change_points, candidate_context_change_points)
    pre_active_context_similarity = jaccard(
        set(query_background_provenance["pre_active_contexts"]),
        set(candidate_background_provenance["pre_active_contexts"]),
    )
    post_active_context_similarity = jaccard(
        set(query_background_provenance["post_active_contexts"]),
        set(candidate_background_provenance["post_active_contexts"]),
    )
    provenance_scores = []
    if query_background_provenance["pre_active_count"] > 0 or candidate_background_provenance["pre_active_count"] > 0:
        provenance_scores.append(pre_active_context_similarity)
    if query_background_provenance["post_active_count"] > 0 or candidate_background_provenance["post_active_count"] > 0:
        provenance_scores.append(post_active_context_similarity)
    background_provenance_similarity = (
        sum(provenance_scores) / len(provenance_scores)
        if provenance_scores
        else 1.0
    )
    subject_set_similarity = jaccard(
        set(query_subject_profile["subjects"]),
        set(candidate_subject_profile["subjects"]),
    )
    first_active_timing_similarity = 1.0
    if (
        query_subject_profile["first_active_frame"] is not None
        and candidate_subject_profile["first_active_frame"] is not None
    ):
        max_first_frame = max(
            1,
            query_subject_profile["first_active_frame"],
            candidate_subject_profile["first_active_frame"],
        )
        first_active_timing_similarity = max(
            0.0,
            1.0
            - abs(
                query_subject_profile["first_active_frame"]
                - candidate_subject_profile["first_active_frame"]
            )
            / max_first_frame,
        )
    subject_persistence_similarity = 1.0
    shared_subjects = set(query_subject_profile["subjects"]) & set(candidate_subject_profile["subjects"])
    if shared_subjects:
        persistence_scores = []
        for subject in sorted(shared_subjects):
            q_len = query_subject_profile["active_run_lengths"].get(subject, 0)
            c_len = candidate_subject_profile["active_run_lengths"].get(subject, 0)
            max_len = max(1, q_len, c_len)
            persistence_scores.append(1.0 - abs(q_len - c_len) / max_len)
        subject_persistence_similarity = sum(persistence_scores) / len(persistence_scores)
    elif query_subject_profile["subjects"] or candidate_subject_profile["subjects"]:
        subject_persistence_similarity = 0.0
    last_active_timing_similarity = 1.0
    if (
        query_subject_profile["last_active_frame"] is not None
        and candidate_subject_profile["last_active_frame"] is not None
    ):
        max_last_frame = max(
            1,
            query_subject_profile["last_active_frame"],
            candidate_subject_profile["last_active_frame"],
        )
        last_active_timing_similarity = max(
            0.0,
            1.0
            - abs(
                query_subject_profile["last_active_frame"]
                - candidate_subject_profile["last_active_frame"]
            )
            / max_last_frame,
        )
    background_recovery_similarity = 1.0
    q_recovery = query_subject_profile["background_recovery_count"]
    c_recovery = candidate_subject_profile["background_recovery_count"]
    if q_recovery or c_recovery:
        max_recovery = max(1, q_recovery, c_recovery)
        background_recovery_similarity = 1.0 - abs(q_recovery - c_recovery) / max_recovery
    phase_prefix_matches = 0
    for q_phase, c_phase in zip(query_phase_sequence, candidate_phase_sequence):
        if q_phase != c_phase:
            break
        phase_prefix_matches += 1
    max_phase_len = max(1, len(query_phase_sequence), len(candidate_phase_sequence))
    phase_sequence_similarity = phase_prefix_matches / max_phase_len
    phase_count_similarity = 1.0 - abs(len(query_phase_sequence) - len(candidate_phase_sequence)) / max_phase_len
    state_prefix_matches = 0
    for q_state, c_state in zip(query_state_sequence, candidate_state_sequence):
        if q_state != c_state:
            break
        state_prefix_matches += 1
    max_state_len = max(1, len(query_state_sequence), len(candidate_state_sequence))
    state_sequence_similarity = state_prefix_matches / max_state_len
    state_count_similarity = 1.0 - abs(len(query_state_sequence) - len(candidate_state_sequence)) / max_state_len
    transition_pair_prefix_matches = 0
    for q_pair, c_pair in zip(query_transition_pairs, candidate_transition_pairs):
        if q_pair != c_pair:
            break
        transition_pair_prefix_matches += 1
    max_pair_len = max(1, len(query_transition_pairs), len(candidate_transition_pairs))
    transition_pair_similarity = transition_pair_prefix_matches / max_pair_len
    transition_pair_count_similarity = 1.0 - abs(len(query_transition_pairs) - len(candidate_transition_pairs)) / max_pair_len
    timed_transition_pair_prefix_matches = 0
    for q_pair, c_pair in zip(query_timed_transition_pairs, candidate_timed_transition_pairs):
        if q_pair != c_pair:
            break
        timed_transition_pair_prefix_matches += 1
    max_timed_pair_len = max(1, len(query_timed_transition_pairs), len(candidate_timed_transition_pairs))
    timed_transition_pair_similarity = timed_transition_pair_prefix_matches / max_timed_pair_len
    timed_transition_pair_count_similarity = 1.0 - abs(
        len(query_timed_transition_pairs) - len(candidate_timed_transition_pairs)
    ) / max_timed_pair_len
    subject_activity_prefix_matches = 0
    for q_item, c_item in zip(query_subject_activity_sequence, candidate_subject_activity_sequence):
        if q_item != c_item:
            break
        subject_activity_prefix_matches += 1
    max_subject_activity_len = max(1, len(query_subject_activity_sequence), len(candidate_subject_activity_sequence))
    subject_activity_sequence_similarity = subject_activity_prefix_matches / max_subject_activity_len
    subject_activity_count_similarity = 1.0 - abs(
        len(query_subject_activity_sequence) - len(candidate_subject_activity_sequence)
    ) / max_subject_activity_len
    borrowed_background_risk = 0.0
    if (
        not background_only_query
        and len(query_active_frames) == 1
        and query_frame_count > 1
        and shared_active_frame_coverage >= 1.0
    ):
        borrowed_background_risk = min(
            1.0,
            (
                (1.0 - shared_frame_coverage) * 0.35
                + (1.0 - context_set_similarity) * 0.15
                + (1.0 - context_transition_similarity) * 0.5
            ),
        )

    borrowed_background_mismatch = borrowed_background_risk * (1.0 - background_provenance_similarity)
    sparse_active_evidence_penalty = 0.0
    if not background_only_query and query_frame_count > 1 and len(query_active_frames) == 1:
        active_ratio = len(query_active_frames) / query_frame_count
        sparse_active_evidence_penalty = (1.0 - active_ratio) * 12.0
        if query_transition_points:
            sparse_active_evidence_penalty += max(0.0, 2.0 - len(query_transition_points)) * 2.0
    fragmented_active_coverage_penalty = 0.0
    if not background_only_query and len(query_active_frames) > 1:
        fragmented_active_coverage_penalty = (
            (1.0 - shared_active_frame_coverage) * 14.0
            + (1.0 - active_state_set_similarity) * 10.0
            + (1.0 - subject_activity_sequence_similarity) * 8.0
        )
    active_semantic_diversity_penalty = 0.0
    if not background_only_query and len(query_active_states) > 1:
        active_semantic_diversity_penalty = (
            max(0.0, len(query_active_states) - len(candidate_active_states)) * 6.0
            + (1.0 - active_state_set_similarity) * 12.0
            + (1.0 - active_pose_set_similarity) * 8.0
            + (1.0 - subject_activity_sequence_similarity) * 10.0
        )
    active_island_penalty = 0.0
    query_profile_data = query_profile(query)
    if not background_only_query and len(query_active_frames) == 1 and query_frame_count > 1:
        active_island_penalty = float(query_profile_data.get("active_island_risk", 0.0)) * 12.0
    borrowed_background_context_penalty = 0.0
    if (
        not background_only_query
        and len(query_active_frames) == 1
        and query_frame_count > 1
        and borrowed_background_risk > 0.0
    ):
        borrowed_background_context_penalty = (
            borrowed_background_risk * 8.0
            + borrowed_background_mismatch * 10.0
            + (1.0 - float(query_profile_data.get("active_context_novelty", 1.0))) * 10.0
        )
    blended_active_narrative_penalty = 0.0
    if not background_only_query and len(query_active_frames) > 1:
        blended_active_narrative_penalty = (
            (1.0 - float(query_profile_data.get("active_context_novelty", 1.0))) * 8.0
            + (1.0 - shared_active_frame_coverage) * 10.0
            + (1.0 - context_transition_similarity) * 6.0
        )
    single_active_alignment_penalty = 0.0
    if not background_only_query and len(query_active_frames) == 1 and query_frame_count > 1:
        single_active_alignment_penalty = (
            shared_active_frame_coverage * 6.0
            + exact_active_state_matches * 4.0
            + exact_active_primary_subject_matches * 3.0
            + exact_active_pose_matches * 3.0
        ) * (1.0 - background_provenance_similarity)
    low_novelty_multi_active_penalty = 0.0
    if not background_only_query and len(query_active_frames) > 1:
        low_novelty_multi_active_penalty = (
            (1.0 - float(query_profile_data.get("active_context_novelty", 1.0))) * 10.0
            + (1.0 - shared_active_frame_coverage) * 8.0
            + (1.0 - context_set_similarity) * 6.0
        )
    late_active_island_context_penalty = 0.0
    if not background_only_query and len(query_active_frames) == 1 and query_frame_count > 1:
        late_active_island_context_penalty = (
            float(query_profile_data.get("active_island_risk", 0.0)) * 8.0
            + (1.0 - float(query_profile_data.get("active_context_novelty", 1.0))) * 8.0
        ) * (1.0 - background_provenance_similarity)

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
    score += exact_frame_signature_matches * (2.0 if background_only_query else 10.0)
    if background_only_query:
        score += exact_state_matches * 0.0
        score += exact_context_matches * 0.0
        score += token_similarity * 0.0
        score += activity_similarity * 0.0
        score += shared_frame_coverage * 0.0
        score += context_set_similarity * 0.0
    else:
        score += exact_state_matches * (3.0 if has_active_alignment else 1.5)
        score += exact_context_matches * (2.0 if has_active_alignment else 0.5)
        score += exact_primary_subject_matches * (3.0 if has_active_alignment else 1.0)
        score += token_similarity * (20.0 if has_active_alignment else 10.0)
        score += activity_similarity * (10.0 if has_active_alignment else 4.0)
        score += shared_frame_coverage * (25.0 if has_active_alignment else 8.0)
    score += shared_active_frame_coverage * 34.0
    score += exact_active_state_matches * 16.0
    score += exact_active_primary_subject_matches * 8.0
    score += exact_active_pose_matches * 8.0
    score += 0.0 if background_only_query or not has_active_alignment else active_state_set_similarity * 10.0
    score += 0.0 if background_only_query or not has_active_alignment else active_pose_set_similarity * 6.0
    score += (0.0 if background_only_query else 8.0) if family_match else 0.0
    score += (
        0.0
        if background_only_query
        else pose_similarity * (12.0 if has_active_alignment else 4.0)
    )
    score += (0.0 if background_only_query else 8.0) * transition_similarity
    score += 0.0 if background_only_query else context_transition_similarity * 6.0
    score += 0.0 if background_only_query or not has_active_alignment else background_provenance_similarity * 8.0
    score += 0.0 if background_only_query or not has_active_alignment else subject_set_similarity * 8.0
    score += 0.0 if background_only_query or not has_active_alignment else first_active_timing_similarity * 10.0
    score += 0.0 if background_only_query or not has_active_alignment else last_active_timing_similarity * 8.0
    score += 0.0 if background_only_query or not has_active_alignment else subject_persistence_similarity * 8.0
    score += 0.0 if background_only_query or not has_active_alignment else background_recovery_similarity * 8.0
    score += 0.0 if background_only_query or not has_active_alignment else phase_sequence_similarity * 10.0
    score += 0.0 if background_only_query or not has_active_alignment else phase_count_similarity * 6.0
    score += 0.0 if background_only_query or not has_active_alignment else state_sequence_similarity * 10.0
    score += 0.0 if background_only_query or not has_active_alignment else state_count_similarity * 6.0
    score += 0.0 if background_only_query or not has_active_alignment else transition_pair_similarity * 10.0
    score += 0.0 if background_only_query or not has_active_alignment else transition_pair_count_similarity * 6.0
    score += 0.0 if background_only_query or not has_active_alignment else timed_transition_pair_similarity * 10.0
    score += 0.0 if background_only_query or not has_active_alignment else timed_transition_pair_count_similarity * 6.0
    score += 0.0 if background_only_query or not has_active_alignment else subject_activity_sequence_similarity * 10.0
    score += 0.0 if background_only_query or not has_active_alignment else subject_activity_count_similarity * 6.0
    score += (0.0 if background_only_query else 12.0) * trait_similarity
    if query.get("scene_family") in (None, "", "unknown") and not background_only_query:
        contamination_risk = query_profile(query)["contamination_risk"]
        score -= exact_state_matches * 1.0
        score -= exact_context_matches * 1.0
        score -= exact_primary_subject_matches * 2.0
        score -= exact_active_state_matches * 10.0
        score -= exact_active_primary_subject_matches * 5.0
        score -= exact_active_pose_matches * 3.0
        score -= active_state_set_similarity * 8.0
        score -= active_pose_set_similarity * 6.0
        score -= pose_similarity * (6.0 if has_active_alignment else 2.0)
        score -= token_similarity * (6.0 if has_active_alignment else 3.0)
        score -= activity_similarity * (4.0 if has_active_alignment else 2.0)
        score -= context_set_similarity * 4.0
        score -= (1.0 - transition_similarity) * 6.0
        score -= (1.0 - context_transition_similarity) * 4.0
        score -= background_provenance_similarity * 8.0
        score -= subject_set_similarity * 7.0
        score -= first_active_timing_similarity * 5.0
        score -= last_active_timing_similarity * 4.0
        score -= subject_persistence_similarity * 5.0
        score -= background_recovery_similarity * 4.0
        score -= phase_sequence_similarity * 6.0
        score -= phase_count_similarity * 4.0
        score -= state_sequence_similarity * 8.0
        score -= state_count_similarity * 5.0
        score -= transition_pair_similarity * 10.0
        score -= transition_pair_count_similarity * 6.0
        score -= timed_transition_pair_similarity * 12.0
        score -= timed_transition_pair_count_similarity * 7.0
        score -= subject_activity_sequence_similarity * 14.0
        score -= subject_activity_count_similarity * 8.0
        score -= trait_similarity * 12.0
        score -= borrowed_background_risk * 20.0
        score -= borrowed_background_mismatch * 24.0
        score -= contamination_risk * 10.0
        score -= sparse_active_evidence_penalty
        score -= fragmented_active_coverage_penalty
        score -= active_semantic_diversity_penalty
        score -= active_island_penalty * 1.75
        score -= borrowed_background_context_penalty
        score -= blended_active_narrative_penalty
        score -= single_active_alignment_penalty
        score -= low_novelty_multi_active_penalty
        score -= late_active_island_context_penalty
        score -= shared_frame_coverage * 8.0
        score -= (1.0 - shared_active_frame_coverage) * 16.0
        if len(query_active_frames) == 1 and query_frame_count > 1:
            score -= 20.0
            score -= (1.0 - context_transition_similarity) * 18.0
        if query_frame_count == len(query_active_frames):
            score -= 8.0
        if len(query_active_states) > 1:
            score -= (1.0 - active_state_set_similarity) * 24.0
        if len(query_pose_set) > 1:
            score -= (1.0 - active_pose_set_similarity) * 12.0
        score = max(0.0, score)

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
        "exact_context_matches": exact_context_matches,
        "exact_primary_subject_matches": exact_primary_subject_matches,
        "exact_active_state_matches": exact_active_state_matches,
        "exact_active_primary_subject_matches": exact_active_primary_subject_matches,
        "exact_active_pose_matches": exact_active_pose_matches,
        "token_similarity": round(token_similarity, 6),
        "activity_similarity": round(activity_similarity, 6),
        "pose_similarity": round(pose_similarity, 6),
        "active_state_set_similarity": round(active_state_set_similarity, 6),
        "active_pose_set_similarity": round(active_pose_set_similarity, 6),
        "context_set_similarity": round(context_set_similarity, 6),
        "transition_similarity": round(transition_similarity, 6),
        "context_transition_similarity": round(context_transition_similarity, 6),
        "pre_active_context_similarity": round(pre_active_context_similarity, 6),
        "post_active_context_similarity": round(post_active_context_similarity, 6),
        "background_provenance_similarity": round(background_provenance_similarity, 6),
        "subject_set_similarity": round(subject_set_similarity, 6),
        "first_active_timing_similarity": round(first_active_timing_similarity, 6),
        "last_active_timing_similarity": round(last_active_timing_similarity, 6),
        "subject_persistence_similarity": round(subject_persistence_similarity, 6),
        "background_recovery_similarity": round(background_recovery_similarity, 6),
        "phase_sequence_similarity": round(phase_sequence_similarity, 6),
        "phase_count_similarity": round(phase_count_similarity, 6),
        "state_sequence_similarity": round(state_sequence_similarity, 6),
        "state_count_similarity": round(state_count_similarity, 6),
        "transition_pair_similarity": round(transition_pair_similarity, 6),
        "transition_pair_count_similarity": round(transition_pair_count_similarity, 6),
        "timed_transition_pair_similarity": round(timed_transition_pair_similarity, 6),
        "timed_transition_pair_count_similarity": round(timed_transition_pair_count_similarity, 6),
        "subject_activity_sequence_similarity": round(subject_activity_sequence_similarity, 6),
        "subject_activity_count_similarity": round(subject_activity_count_similarity, 6),
        "borrowed_background_risk": round(borrowed_background_risk, 6),
        "borrowed_background_mismatch": round(borrowed_background_mismatch, 6),
        "sparse_active_evidence_penalty": round(sparse_active_evidence_penalty, 6),
        "fragmented_active_coverage_penalty": round(fragmented_active_coverage_penalty, 6),
        "active_semantic_diversity_penalty": round(active_semantic_diversity_penalty, 6),
        "active_island_penalty": round(active_island_penalty, 6),
        "borrowed_background_context_penalty": round(borrowed_background_context_penalty, 6),
        "blended_active_narrative_penalty": round(blended_active_narrative_penalty, 6),
        "single_active_alignment_penalty": round(single_active_alignment_penalty, 6),
        "low_novelty_multi_active_penalty": round(low_novelty_multi_active_penalty, 6),
        "late_active_island_context_penalty": round(late_active_island_context_penalty, 6),
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
        contamination_risk = float(profile.get("contamination_risk", 0.0))
        if contamination_risk >= 0.6 and active_row_count == 1 and profile["frame_count"] > 1:
            return "ambiguous", f"unknown-family contaminated mixed query risk {contamination_risk:.3f}"
        if float(profile.get("active_island_risk", 0.0)) >= 0.5 and active_row_count == 1 and profile["frame_count"] > 1:
            return "ambiguous", f"unknown-family late active island risk {float(profile.get('active_island_risk', 0.0)):.3f}"
        if float(profile.get("active_frame_ratio", 0.0)) <= 0.34 and active_row_count == 1 and profile["frame_count"] > 1:
            return "ambiguous", f"unknown-family sparse active evidence ratio {float(profile.get('active_frame_ratio', 0.0)):.3f}"
        borrowed_background_risk = float(best.get("borrowed_background_risk", 0.0))
        if borrowed_background_risk >= 0.5 and active_row_count == 1 and profile["frame_count"] > 1:
            return "ambiguous", f"unknown-family borrowed-background mix risk {borrowed_background_risk:.3f}"
        if score >= 110.0 and margin >= 60.0 and ratio is not None and ratio >= 3.0:
            return "identified", f"strong unknown-family score {score:.3f} margin {margin:.3f}"
        if score >= 40.0 and margin >= 100.0:
            return "ambiguous", f"unknown-family strongly separated low-score match {score:.3f} margin {margin:.3f}"
        if score >= 50.0 and margin >= 80.0:
            return "ambiguous", f"unknown-family separated low-score match {score:.3f} margin {margin:.3f}"
        if score >= 60.0 and margin >= 20.0:
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
