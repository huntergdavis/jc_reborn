# Local Vision Classifier Plan

Date: 2026-03-29

Worktree:
- `/tmp/jc_reborn_ps1_debug`

Branch:
- `worktree-ps1-debug-20260329`

Goal:
- build a local visual analysis pipeline for PS1 scene validation and debugging
- run under severe local constraints:
  - about `8 GB` system RAM
  - effectively no useful GPU
  - CPU-only inference is acceptable
  - slow inference is acceptable
- use the canonical Linux/reference scene captures as the semantic source of truth
- pre-run the classifier on those references so PS1 evaluation is reference-driven instead of free-form

This is not primarily a training problem yet.
The first target is a practical semantic harness that converts frames into structured, comparable scene descriptions.

## Executive Recommendation

Do this as a reference-first system, not a generic captioning toy.

The classifier should not try to infer the world from scratch for every PS1 frame.
Instead, it should:

1. extract cheap visual signatures from every canonical reference frame
2. precompute lightweight learned embeddings for those reference frames
3. assign semantic labels to reference frames once
4. evaluate PS1 frames by comparing them against that prepared reference bank
5. only use a small VLM on a sampled subset of frames where we need explanations

That gives us three layers:

1. deterministic triage
2. reference-bank retrieval and semantic matching
3. sampled-frame textual explanation

## Why This Is The Right Shape

We already know a lot:

- the canonical scene list
- the canonical reference images
- which frame ranges matter
- the common PS1 failure modes:
  - black screen
  - title persistence
  - ocean-only background
  - wrong family
  - sprites missing
  - sprites present but wrong timing/content

That means the cheapest useful classifier is not “train a world model.”
It is:

- nearest-neighbor against the known good image bank
- plus a small amount of semantic labeling

This also fits the machine constraints much better than training or running a large VLM over every frame.

## Source Of Truth

Canonical reference runs:

- `/home/hunter/workspace/jc_reborn/regtest-references`

Scene list:

- `/home/hunter/workspace/jc_reborn/config/ps1/regtest-scenes.txt`

Related existing scripts:

- [/home/hunter/workspace/jc_reborn/scripts/build-reference-index.py](/home/hunter/workspace/jc_reborn/scripts/build-reference-index.py)
- [/home/hunter/workspace/jc_reborn/scripts/calibrate-visual-detect.py](/home/hunter/workspace/jc_reborn/scripts/calibrate-visual-detect.py)
- [/home/hunter/workspace/jc_reborn/scripts/compare-reference-batch.sh](/home/hunter/workspace/jc_reborn/scripts/compare-reference-batch.sh)
- [/home/hunter/workspace/jc_reborn/scripts/render-compare-timeline.py](/home/hunter/workspace/jc_reborn/scripts/render-compare-timeline.py)

Validation target:

- `63` scenes

The classifier is not the source of truth.
The reference captures are.
The classifier is a semantic layer that makes the truth usable for automated triage and debugging.

## What The Harness Must Answer

Per frame, we need a compact schema like:

```json
{
  "screen_type": "title|black|ocean|island|scene|transition|unknown",
  "scene_family_guess": "ACTIVITY|BUILDING|WALKSTUF|FISHING|TITLE|unknown",
  "sprites_visible": true,
  "sprite_density": "none|low|medium|high",
  "actors": [
    {
      "name": "johnny",
      "present": true,
      "position": "left|center|right|upper-left|upper-right|lower-left|lower-right",
      "confidence": 0.83
    }
  ],
  "objects": [
    {
      "name": "raft",
      "present": false,
      "position": null,
      "confidence": 0.21
    }
  ],
  "action_summary": "Johnny is standing near the shoreline.",
  "reference_match": {
    "scene": "ACTIVITY-1",
    "frame": "frame_06000.png",
    "distance": 0.12,
    "confidence": 0.78
  },
  "notes": [
    "background only",
    "no active sprite composition visible"
  ]
}
```

Important:

- position buckets are enough initially
- we do not need perfect bounding boxes
- we do not need perfect actor names in phase one
- we do need stable “ocean-only vs actual scene” answers

## Core Design: Reference-First Semantic Retrieval

The canonical references should be processed offline into a reusable semantic bank.

### Reference Bank Contents

For each canonical frame:

- file path
- scene id
- frame number
- cheap deterministic metrics
- embedding vector
- optional text summary
- optional semantic labels

Example:

```json
{
  "scene_id": "ACTIVITY-1",
  "frame": "frame_06000.png",
  "embedding_model": "clip-vit-base-patch32",
  "embedding": "...",
  "metrics": {
    "mean_rgb": [34, 88, 120],
    "water_ratio": 0.41,
    "dark_ratio": 0.03,
    "edge_density": 0.19,
    "telemetry_present": true
  },
  "labels": {
    "screen_type": "scene",
    "scene_family_guess": "ACTIVITY",
    "sprites_visible": true,
    "johnny_present": true,
    "mary_present": false,
    "raft_present": false
  },
  "caption": "Johnny stands on the beach near the center of the frame."
}
```

### Runtime PS1 Evaluation

For each PS1 frame:

1. compute deterministic metrics
2. compute embedding
3. find nearest reference frames globally and within target scene
4. compare semantic labels against nearest reference cluster
5. emit:
   - best reference match
   - semantic mismatch reasons
   - confidence

This gives us answers like:

- “PS1 frame most closely matches ACTIVITY-1 frame_06000 background, but sprites are missing”
- “PS1 frame is closer to FISHING than ACTIVITY”
- “PS1 frame is title-like even though story scene should have launched”

## Architecture

### Stage 0: Build The Reference Index

Create a new offline indexer:

- input:
  - `regtest-references/`
- output:
  - `regtest-references/index.json`
  - `regtest-references/embeddings/*.npy` or a compact shard format
  - optional sampled captions

This stage should:

- enumerate all reference frames
- attach scene/frame metadata
- compute cheap deterministic signatures
- compute embeddings once
- optionally generate labels for a sampled subset

### Stage 1: Cheap Deterministic Triage

Use classical CV and fixed heuristics first for:

- black
- title
- ocean-only
- mostly-water
- telemetry presence
- sprite-like foreground density
- transition frames

We already have the beginning of this approach in:

- [/home/hunter/workspace/jc_reborn/scripts/calibrate-visual-detect.py](/home/hunter/workspace/jc_reborn/scripts/calibrate-visual-detect.py)

This stage should be fast and should handle the most common failure modes without any model call.

### Stage 2: Lightweight Embedding Model

Use a small image embedding model to compare frames to the reference bank.

Recommended default:

- `openai/clip-vit-base-patch32`

Why:

- mature
- simple
- good enough for zero-shot and embedding retrieval
- much lighter than a full VLM
- can run on CPU

This stage should answer:

- which reference frame is this closest to
- which scene family cluster is this closest to
- whether PS1 is semantically near the expected reference state

### Stage 3: Sampled Semantic Explainer

Use a small VLM only on selected frames:

- first visible scene frame
- first mismatch frame
- best-match frame
- final frame
- one or two boundary frames around a divergence

This stage produces:

- short caption
- visible actors/objects
- rough action summary
- “background only” vs “live scene content”

Do not run a VLM across every captured frame at first.

### Stage 4: Optional Tiny Supervised Classifier

Once we have enough labeled reference and PS1 frames, train a tiny classifier for:

- screen_type
- sprites_visible
- johnny_present
- mary_present
- likely_scene_family

This stage is optional and should happen only after the earlier pipeline proves which labels are stable and useful.

## Recommended Models Under 8 GB RAM

### 1. CLIP ViT-B/32

Model:

- `openai/clip-vit-base-patch32`

Role:

- default embedding model
- zero-shot classifier for coarse labels

Why:

- lowest-risk default for a reference-bank system
- good for nearest-neighbor retrieval
- no need to train to get immediate value

Use it for labels like:

- `title screen`
- `black screen`
- `ocean background only`
- `beach scene with character`
- `Johnny visible`
- `Mary visible`
- `raft visible`

### 2. Florence-2-base-ft

Model:

- `microsoft/Florence-2-base-ft`

Role:

- sampled-frame semantic explainer
- captioning and object-oriented prompting

Why:

- small by VLM standards
- multi-task prompting is useful
- better explanatory power than CLIP

Use it for:

- sampled mismatch frames
- human-review summaries
- first-pass pseudo-labeling on the reference set

### 3. SmolVLM-500M

Model:

- `HuggingFaceTB/SmolVLM-500M-Instruct`

Role:

- fallback low-memory VLM

Why:

- designed for constrained devices
- smaller than most VLMs
- useful if Florence-2 is too slow or memory-heavy on CPU

### 4. moondream2

Model:

- `vikhyatk/moondream2`

Role:

- optional CPU captioner if its dedicated runtime proves stable locally

Why:

- plausible CPU memory footprint
- popular for lightweight image description

Risk:

- more integration complexity than CLIP
- less attractive as the first default in this repo

## Reference-Grounded Labeling Strategy

This is where we use our existing knowledge aggressively.

### Pass A: Deterministic Label Seed

For all reference frames, assign cheap seed labels:

- black
- title-like
- ocean-heavy
- telemetry-present
- scene-like

### Pass B: Nearest-Neighbor Clustering

Compute embeddings for all reference frames and cluster them by:

- scene family
- visual phase
- sprite density

This gives us prototype groups like:

- ACTIVITY beach with Johnny
- ACTIVITY ocean-only intro
- title/menu
- black/transition
- FISHING island

### Pass C: Sampled VLM Annotation

Do not caption every frame.

Instead caption:

- cluster centers
- cluster outliers
- representative frames per scene
- known important frames:
  - first visible scene frame
  - middle frame
  - final frame

Then propagate those labels through nearest-neighbor groups.

This keeps VLM cost low while still giving us semantic labels for almost the whole reference bank.

### Pass D: Human Correction Loop

Build review pages so we can quickly correct:

- wrong actor names
- wrong family labels
- wrong action summaries

Then persist corrected labels back into the reference bank.

## How PS1 Comparison Should Work

For a PS1 run:

1. identify expected reference scene
2. for each sampled PS1 frame:
   - deterministic triage
   - embedding lookup
   - nearest reference match within expected scene
   - nearest global reference match
3. emit both:
   - `best_expected_match`
   - `best_global_match`

This distinction matters.

Examples:

- expected scene says `ACTIVITY-1`, but global match says `FISHING-3`
- expected scene says `ACTIVITY-1`, expected-match frame is ocean-only and global-match frame is also ocean-only
- expected scene says `ACTIVITY-1`, best expected match is correct background but labels show `sprites_visible = false`

That gives us much better debugging signal than pixel mismatch alone.

## Proposed Output Files

Per reference bank build:

- `regtest-references/index.json`
- `regtest-references/embeddings/clip-vit-base-patch32.f16.npy`
- `regtest-references/semantic-labels.json`
- `regtest-references/captions.json`

Per analyzed PS1 run:

- `vision-analysis.json`
- `vision-summary.json`
- `vision-review.html`

Example `vision-summary.json`:

```json
{
  "scene": "ACTIVITY-1",
  "expected_family": "ACTIVITY",
  "frames_analyzed": 12,
  "best_expected_match_score": 0.81,
  "best_global_match_scene": "ACTIVITY-1",
  "dominant_failure_mode": "background_only_missing_sprites",
  "sprite_visible_ratio": 0.08,
  "title_like_ratio": 0.0,
  "ocean_only_ratio": 0.67,
  "notes": [
    "Background matches expected intro ocean frames.",
    "No later frame matches reference frames with visible Johnny sprites."
  ]
}
```

## What Counts As Success

For scene validation, success is not:

- same last-frame hash
- same black frame
- “looks close enough”

Success is:

- PS1 frames retrieve into the expected reference scene
- the semantic labels broadly agree:
  - sprites visible when reference has sprites
  - same family
  - same coarse phase
  - same actor presence on key frames

For the `63`-scene target, the harness should let us say:

- fully validated
- visually close but semantically wrong
- background-correct but sprite-missing
- wrong-family reroute
- title/black persistence

## Recommended Implementation Order

### Phase 1

Implement:

- `build_reference_bank.py`
- deterministic metric extraction
- CLIP embeddings for references
- nearest-neighbor lookup for PS1 frames

No training yet.

### Phase 2

Implement:

- `analyze_run_vision.py`
- `vision-review.html`
- per-run semantic summaries

### Phase 3

Add sampled VLM labeling:

- Florence-2 default
- SmolVLM fallback

### Phase 4

Only after enough labels exist:

- tiny supervised classifier for stable labels

## Immediate Practical Recommendation

Build the first version around:

1. deterministic metrics
2. CLIP embeddings
3. nearest reference retrieval
4. sampled Florence-2 captions

That is the best tradeoff for this machine and this repo.

It uses what we already know instead of pretending we need a bespoke model first.

## Research Notes

Useful model pages / references:

- `microsoft/Florence-2-base-ft`
- `openai/clip-vit-base-patch32`
- `HuggingFaceTB/SmolVLM-500M-Instruct`
- `vikhyatk/moondream2`

Key design implication from that research:

- CLIP-class retrieval should be the backbone
- tiny VLMs should be used as sparse explainers
- reference-bank preprocessing is where we should spend most of the compute

## Progress Logging

All implementation work for this classifier should log:

- what model was tested
- RAM usage observed
- per-frame inference time
- whether output was actually useful for PS1 debugging
- where false positives happened

That log should live under:

- `/tmp/jc_reborn_ps1_debug/docs/ps1/research/`

