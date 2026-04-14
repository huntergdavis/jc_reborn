# Foreground Timing Plan 2026-04-13

Current validated baseline:

- `fgpilot fishing1` is visually correct.
- Full-scene export is correct.
- Host timing preservation is corrected enough that playback is back near the
  earlier fast baseline instead of the over-slow scheduler-sum path.
- Launch path:
  `./scripts/rebuild-and-let-run.sh fgpilot fishing1`

Validation rule:

1. One change at a time.
2. Clean rebuild and launch with the repo script.
3. Human checks visuals and speed.
4. If visuals are good and speed is same or better, commit and push.

## What Worked

- Entry-table preload.
- Bulk-copy compositor fast path.
- Narrow background restore.
- Full-scene `story-single --until-exit` export.
- Corrected host-timing preservation in the export pipeline.
- Single-tile upload-path tightening.
- Dedicated one-tile upload branch in `grDrawBackground()`.
- Same-bounds fused restore+composite path.

## What Failed

- Direct framebuffer path.
- Diff/direct-delta runtime path.
- Progressive test path.
- Overlap-skip restore as implemented.
- Naive scheduler-delay summing.
- Naive read-ahead / double-buffering.
- Pilot-only black-clear restore replacement.

## Prioritized Speed Backlog

1. `grDrawBackground()` / upload cost.
   Best remaining likely win on the stable path.

2. Safer background restore reduction.
   Retry only with stricter invariants than the broken overlap-skip version.

3. Pack-time precompute for runtime blits/restores.
   Tile coverage, row offsets, clipped spans.

4. Same-rect / same-shape cached setup.
   Reuse setup when consecutive frames share bounds.

5. Held-frame no-work path.
   If the frame is held, do the minimum possible.

6. More specialized memcpy-based foreground blit paths.
   Keep squeezing the stable compositor path.

7. Better CD read path, but not naive read-ahead.
   Any future buffering must preserve exact ownership and frame identity.

8. Pack layout improvements.
   Runtime-friendly ordering and alignment.

9. More timing work only after more render throughput wins.
   The biggest timing mistake is fixed for now.

10. Display mode experiments last.
    Do not revisit direct/progressive until the stable path is exhausted.

## Next Target

Stay on the stable compositor path and keep attacking the heavy early-motion
section of the scene, where the tall tree-walk rect still feels slower than the
later action. Favor changes that reduce restore/upload work for larger active
areas before returning to CD buffering experiments.

## Repeat Prompt

> Continue PS1 Fishing 1 foreground speed improvements from the current repo state. Stay on the stable `fgpilot fishing1` path only. Make one high-impact safe improvement from `docs/ps1/research/FOREGROUND_TIMING_PLAN_2026-04-13.md`, launch it with `./scripts/rebuild-and-let-run.sh fgpilot fishing1`, and stop for my validation. Do not use experimental direct framebuffer, diff, or progressive paths unless explicitly asked.
