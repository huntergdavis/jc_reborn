# PS1 Research — Historical Design Logs

**This directory is a design / decision archive, not current truth.**

For current operator documentation use:
- [../README.md](../README.md) — PS1 branch entrypoint
- [../current-status.md](../current-status.md) — project narrative
- [../scene-status.md](../scene-status.md) — per-scene ledger
- [../development-workflow.md](../development-workflow.md) — the bring-up loop
- [../TESTING.md](../TESTING.md) — validation strategy

Everything below is preserved as the paper trail for how the branch got
to its current state. Historical status counts (`25/63`, `27/63`,
`57/63`, `60/63`, `63/63`) quoted in docs under this directory reflect
earlier validation models that have since been demoted — see
`../current-status.md` § "Historical status numbers (not current)" for
the reconciliation, and `../ps1-branch-cleanup-plan.yaml` §
`historical_status_surfaces_and_meanings` for the rationale.

## Chronology

### 2026-03 — rendering simplification research
Originally kicked off the offline-authored scene-pack / restore-pilot
direction that eventually led to the current hybrid playback path.
- [CURRENT_STATUS_2026-03-21.md](CURRENT_STATUS_2026-03-21.md) —
  restore-rollout status snapshot. Historical.
- [CURRENT_STATUS_2026-03-21.json](CURRENT_STATUS_2026-03-21.json) —
  machine-readable version of the above. Historical.
- [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) — execution plan for
  the restore-pilot pipeline.
- [BACKLOG.md](BACKLOG.md) — parallelizable work tracks as understood at
  the time.
- [ROLLOUT_HISTORY.md](ROLLOUT_HISTORY.md) — dated diary.
- `generated/` — bulk generated datasets (scene analyses, pack manifests,
  dirty-region templates) from this era.
- `archive/` — superseded snapshots and pilot artifacts.

### 2026-03-29 — validation instrumentation + "63/63" false summit
- [AGENT_PROMPT_2026-03-29.md](AGENT_PROMPT_2026-03-29.md) — validation
  prompt discipline.
- [VALIDATION_LOG_2026-03-29.md](VALIDATION_LOG_2026-03-29.md) — claimed
  `63 / 63` under the harness definition; retroactively demoted because
  the harness definition did not match human scene-correctness.
- [HARNESS_WORKLOG_2026-03-28.md](HARNESS_WORKLOG_2026-03-28.md) —
  worklog for the harness build.

### 2026-04-12 — offline scene playback pivot
The branch's turning point. The generic restore-pilot model is de-emphasized
in favour of offline-authored foreground playback with a narrow PS1
runtime surface.
- [OFFLINE_SCENE_PLAYBACK_PIVOT_2026-04-12.md](OFFLINE_SCENE_PLAYBACK_PIVOT_2026-04-12.md)
- [FOREGROUND_TIMING_PLAN_2026-04-13.md](FOREGROUND_TIMING_PLAN_2026-04-13.md)
- [FOREGROUND_TOP_LAYER_LOG_2026-04-13.md](FOREGROUND_TOP_LAYER_LOG_2026-04-13.md)

### 2026-04-21 — fishing1 pixel-perfect push
- [FISHING1_PIXEL_PERFECT_PLAN_2026-04-21.md](FISHING1_PIXEL_PERFECT_PLAN_2026-04-21.md)
- [WATER_ANIMATION_HANDOFF_2026-04-21.md](WATER_ANIMATION_HANDOFF_2026-04-21.md)
- [WATER_ANIMATION_WORKLOG_2026-04-21.md](WATER_ANIMATION_WORKLOG_2026-04-21.md)

### 2026-04-22 — fishing1 full SFX + `v0.3.6-ps1`
Captured in the main `current-status.md` and `scene-status.md`. No
separate research doc — the commits (`355227fa`, `f2737253`) and the
scene-status ledger are the truth surface going forward.

## Schemas + reference (historical but still accurate where referenced)

- [SDL_COMPAT_LITE_SPEC.md](SDL_COMPAT_LITE_SPEC.md)
- [PACK_MANIFEST_SCHEMA.md](PACK_MANIFEST_SCHEMA.md)
- [PACK_PAYLOAD_LAYOUT.md](PACK_PAYLOAD_LAYOUT.md)
- [TRANSITION_PREFETCH_SCHEMA.md](TRANSITION_PREFETCH_SCHEMA.md)
- [DIRTY_REGION_TEMPLATE_SCHEMA.md](DIRTY_REGION_TEMPLATE_SCHEMA.md)

These describe contracts from the restore-pilot era. The current
foreground-pack (FGP v2) format is defined inline in
`scripts/build-scene-foreground-pack.py` and consumed by
`foreground_pilot.c`.

## Directory layout

- Root `research/` — this index and the design logs above.
- `generated/` — bulk machine-generated datasets (preserved as
  archaeology; not part of the current build).
- `archive/` — superseded snapshots and pilot artifacts.

## See also

- [../ps1-branch-cleanup-plan.yaml](../ps1-branch-cleanup-plan.yaml) —
  in-flight cleanup contract; this directory is part of the "split
  current vs historical research" phase.
