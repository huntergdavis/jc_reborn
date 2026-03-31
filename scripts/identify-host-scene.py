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


def compare_scenes(query: dict, candidate: dict) -> dict:
    query_rows = rows_by_frame(query)
    cand_rows = rows_by_frame(candidate)
    shared_frames = sorted(set(query_rows) & set(cand_rows))

    exact_frame_signature_matches = 0
    exact_state_matches = 0
    exact_primary_subject_matches = 0
    token_similarity_sum = 0.0
    activity_overlap_sum = 0.0

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

    shared_count = len(shared_frames)
    token_similarity = (token_similarity_sum / shared_count) if shared_count else 0.0
    activity_similarity = (activity_overlap_sum / shared_count) if shared_count else 0.0

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
    score += exact_state_matches * 3.0
    score += exact_primary_subject_matches * 2.0
    score += token_similarity * 20.0
    score += activity_similarity * 10.0
    score += 5.0 if family_match else 0.0
    score += trait_similarity * 10.0

    return {
        "scene_label": candidate.get("scene_label"),
        "scene_family": candidate.get("scene_family"),
        "score": round(score, 6),
        "exact_scene_signature": exact_scene_signature,
        "family_match": family_match,
        "shared_frame_count": shared_count,
        "exact_frame_signature_matches": exact_frame_signature_matches,
        "exact_state_matches": exact_state_matches,
        "exact_primary_subject_matches": exact_primary_subject_matches,
        "token_similarity": round(token_similarity, 6),
        "activity_similarity": round(activity_similarity, 6),
        "trait_similarity": round(trait_similarity, 6),
        "candidate_scene_signature": (candidate.get("scene_summary") or {}).get("scene_signature"),
    }


def identify(database: dict, query: dict) -> dict:
    database_scenes = database.get("scenes", [])
    rows = []
    for query_scene in query.get("scenes", []):
        matches = [compare_scenes(query_scene, candidate) for candidate in database_scenes]
        matches.sort(key=lambda item: (-item["score"], item["scene_label"] or ""))
        best = matches[0] if matches else None
        rows.append(
            {
                "query_scene_label": query_scene.get("scene_label"),
                "query_scene_family": query_scene.get("scene_family"),
                "query_scene_signature": (query_scene.get("scene_summary") or {}).get("scene_signature"),
                "best_match": best,
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
