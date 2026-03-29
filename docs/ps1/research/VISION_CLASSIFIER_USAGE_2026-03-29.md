# Vision Classifier Usage

Worktree:
- `/tmp/jc_reborn_ps1_debug`

## One-Command Reference Pipeline

Run:

```bash
/tmp/jc_reborn_ps1_debug/scripts/run-vision-reference-pipeline.sh
```

Optional custom output root:

```bash
/tmp/jc_reborn_ps1_debug/scripts/run-vision-reference-pipeline.sh \
  /home/hunter/workspace/jc_reborn/vision-artifacts/custom-reference-pipeline
```

## Primary Outputs

Top-level published entry:

- `/home/hunter/workspace/jc_reborn/vision-artifacts/vision-reference-pipeline-current/index.html`

Reference bank:

- `/tmp/jc_reborn_ps1_debug/artifacts/vision-reference-bank-20260329/index.html`

Latest self-check:

- `/home/hunter/workspace/jc_reborn/vision-artifacts/vision-reference-selfcheck-20260329-v4/index.html`

## Useful Reports

- quality: `/home/hunter/workspace/jc_reborn/vision-artifacts/vision-reference-selfcheck-20260329-v4/quality-report.html`
- confusion: `/home/hunter/workspace/jc_reborn/vision-artifacts/vision-reference-selfcheck-20260329-v4/confusion-report.html`
- family: `/home/hunter/workspace/jc_reborn/vision-artifacts/vision-reference-selfcheck-20260329-v4/family-report.html`
- inventory: `/home/hunter/workspace/jc_reborn/vision-artifacts/vision-reference-pipeline-current/scene-inventory.html`

## Core CLI

Build bank:

```bash
python3 /tmp/jc_reborn_ps1_debug/scripts/vision_classifier.py \
  build-reference-bank \
  --refdir /home/hunter/workspace/jc_reborn/regtest-references \
  --outdir /tmp/jc_reborn_ps1_debug/artifacts/vision-reference-bank-20260329
```

Analyze one run:

```bash
python3 /tmp/jc_reborn_ps1_debug/scripts/vision_classifier.py \
  analyze-run \
  --scene-dir /home/hunter/workspace/jc_reborn/regtest-references/ACTIVITY-1 \
  --bank-dir /tmp/jc_reborn_ps1_debug/artifacts/vision-reference-bank-20260329 \
  --outdir /home/hunter/workspace/jc_reborn/vision-artifacts/example-run \
  --expected-scene ACTIVITY-1
```

Analyze all reference scenes against the bank:

```bash
python3 /tmp/jc_reborn_ps1_debug/scripts/vision_classifier.py \
  analyze-reference-set \
  --refdir /home/hunter/workspace/jc_reborn/regtest-references \
  --bank-dir /tmp/jc_reborn_ps1_debug/artifacts/vision-reference-bank-20260329 \
  --outdir /home/hunter/workspace/jc_reborn/vision-artifacts/vision-reference-selfcheck-20260329-v4
```

Publish top-level entry pages:

```bash
python3 /tmp/jc_reborn_ps1_debug/scripts/publish-vision-pipeline.py
```

## Important Note

This pipeline is complete on the reference side.
The next meaningful use is PS1 analysis against the built reference bank.
