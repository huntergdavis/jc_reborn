# Vision Classifier Worklog

Date:
- 2026-03-29

Worktree:
- `/tmp/jc_reborn_ps1_debug`

Branch:
- `worktree-ps1-debug-20260329`

## Goal

Build a local CPU-only visual classifier pipeline that:

- uses the canonical reference scenes as source of truth
- runs under low-memory constraints
- produces structured semantic artifacts
- generates human-reviewable HTML pages
- scales across all `63` reference scenes

## Constraints

- effectively no usable GPU
- about `8 GB` RAM
- avoid heavyweight runtime dependencies
- acceptable to trade speed for robustness

## Decisions

The pipeline is reference-first, not training-first.

It uses:

- deterministic visual features
- lightweight per-frame feature vectors
- nearest-reference retrieval against the canonical bank
- semantic summaries derived from reference-bank matches

It deliberately does not depend on:

- `torch`
- `transformers`
- `sklearn`

Current runtime dependencies are:

- `Pillow`
- `NumPy`

## Implemented

Primary code:

- `/tmp/jc_reborn_ps1_debug/scripts/vision_classifier.py`
- `/tmp/jc_reborn_ps1_debug/scripts/run-vision-reference-pipeline.sh`
- `/tmp/jc_reborn_ps1_debug/scripts/publish-vision-pipeline.py`

Design doc:

- `/tmp/jc_reborn_ps1_debug/docs/ps1/research/LOCAL_VISION_CLASSIFIER_PLAN_2026-03-29.md`

## Completed Outputs

Reference bank:

- `/tmp/jc_reborn_ps1_debug/artifacts/vision-reference-bank-20260329/`

Reference self-check, latest:

- `/home/hunter/workspace/jc_reborn/vision-artifacts/vision-reference-selfcheck-20260329-v4/`

Published top-level entry:

- `/home/hunter/workspace/jc_reborn/vision-artifacts/vision-reference-pipeline-current/index.html`

## Artifact Types Now Available

For all `63` reference scenes:

- per-scene `review.html`
- per-scene `vision-analysis.json`
- bank-level `index.html`
- self-check `index.html`
- quality report
- confusion report
- family report
- inventory JSON
- inventory HTML
- inventory CSV
- strongest-scenes JSON
- weakest-scenes JSON
- top-level manifest JSON

## Scale Reached

Reference scenes processed:

- `63`

Reference frames indexed:

- `13,128`

## Key Improvements Made

1. Built a full bank over all reference frames.
2. Added per-run analysis against that bank.
3. Added per-scene HTML review outputs.
4. Added full self-check over all `63` references.
5. Tightened label rules to reduce excessive `ocean` classification.
6. Added quality, confusion, and family-level reports.
7. Added export-friendly inventory artifacts and top-level publishing.

## Current Limits

The pipeline is operational, but semantic quality is still v1.

Known weak areas:

- similar `FISHING` scenes cross-match each other
- several `STAND` scenes remain hard to separate
- some scene families still need stronger actor/sprite semantics

So:

- pipeline completeness: good
- artifact completeness: good
- semantic precision: improving, not final

## Next Practical Step

Run this exact pipeline against PS1 result directories and produce:

- PS1 `review.html`
- PS1 `vision-analysis.json`
- PS1 quality summary against the reference bank
- scene ranking by semantic mismatch mode

That is the next step that will directly help fix PS1 scene bugs.
