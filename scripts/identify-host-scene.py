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
    query_subject_profile = subject_timeline_profile(query_rows)
    candidate_subject_profile = subject_timeline_profile(cand_rows)
    query_phase_sequence = phase_sequence_profile(query_rows)
    candidate_phase_sequence = phase_sequence_profile(cand_rows)
    query_state_sequence = state_sequence_profile(query_rows)
    candidate_state_sequence = state_sequence_profile(cand_rows)

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
        score += exact_state_matches * 0.5
        score += exact_context_matches * 0.25
        score += token_similarity * 4.0
        score += activity_similarity * 1.0
        score += shared_frame_coverage * 4.0
        score += context_set_similarity * 8.0
    else:
        score += exact_state_matches * 3.0
        score += exact_context_matches * 1.0
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
    score += transition_similarity * 8.0
    score += 0.0 if background_only_query else context_transition_similarity * 6.0
    score += 0.0 if background_only_query or not has_active_alignment else subject_set_similarity * 8.0
    score += 0.0 if background_only_query or not has_active_alignment else first_active_timing_similarity * 10.0
    score += 0.0 if background_only_query or not has_active_alignment else last_active_timing_similarity * 8.0
    score += 0.0 if background_only_query or not has_active_alignment else subject_persistence_similarity * 8.0
    score += 0.0 if background_only_query or not has_active_alignment else background_recovery_similarity * 8.0
    score += 0.0 if background_only_query or not has_active_alignment else phase_sequence_similarity * 10.0
    score += 0.0 if background_only_query or not has_active_alignment else phase_count_similarity * 6.0
    score += 0.0 if background_only_query or not has_active_alignment else state_sequence_similarity * 10.0
    score += 0.0 if background_only_query or not has_active_alignment else state_count_similarity * 6.0
    score += trait_similarity * 10.0
    if query.get("scene_family") in (None, "", "unknown") and not background_only_query:
        score -= exact_state_matches * 1.0
        score -= exact_context_matches * 0.5
        score -= exact_primary_subject_matches * 1.0
        score -= exact_active_primary_subject_matches * 3.0
        score -= exact_active_pose_matches * 3.0
        score -= pose_similarity * 6.0
        score -= token_similarity * 6.0
        score -= activity_similarity * 4.0
        score -= context_set_similarity * 4.0
        score -= (1.0 - transition_similarity) * 6.0
        score -= (1.0 - context_transition_similarity) * 4.0
        score -= subject_set_similarity * 7.0
        score -= first_active_timing_similarity * 5.0
        score -= last_active_timing_similarity * 4.0
        score -= subject_persistence_similarity * 5.0
        score -= background_recovery_similarity * 4.0
        score -= phase_sequence_similarity * 6.0
        score -= phase_count_similarity * 4.0
        score -= state_sequence_similarity * 8.0
        score -= state_count_similarity * 5.0
        score -= shared_frame_coverage * 8.0
        score -= (1.0 - shared_active_frame_coverage) * 16.0
        if len(query_active_frames) == 1 and query_frame_count > 1:
            score -= 16.0
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
        "subject_set_similarity": round(subject_set_similarity, 6),
        "first_active_timing_similarity": round(first_active_timing_similarity, 6),
        "last_active_timing_similarity": round(last_active_timing_similarity, 6),
        "subject_persistence_similarity": round(subject_persistence_similarity, 6),
        "background_recovery_similarity": round(background_recovery_similarity, 6),
        "phase_sequence_similarity": round(phase_sequence_similarity, 6),
        "phase_count_similarity": round(phase_count_similarity, 6),
        "state_sequence_similarity": round(state_sequence_similarity, 6),
        "state_count_similarity": round(state_count_similarity, 6),
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
