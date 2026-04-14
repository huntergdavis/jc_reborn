# Foreground Timing Plan 2026-04-13

This note captures the current `fgpilot fishing1` baseline and the next timing
investigation steps.

## Baseline

- `fgpilot fishing1` boots and plays the canonical full-frame Fishing 1
  foreground on PS1 through the standard repo launch flow.
- Current validated launch command:
  `./scripts/rebuild-and-let-run.sh fgpilot fishing1`
- Current state is visually correct and recognizably matches the PC scene
  action, but playback still feels materially slower than PC.
- The newer direct framebuffer playback experiment regressed visuals with
  static/ghosting and is not part of the live baseline.
- The live baseline is the compositor-based playback path in
  `foreground_pilot.c`.

## Goal

Match PC scene timing for the PS1 foreground pilot without regressing the
currently-correct visuals.

## Validation Rule

After every single timing change:

1. clean rebuild
2. launch with `./scripts/rebuild-and-let-run.sh fgpilot fishing1`
3. human validation on visuals and speed before proceeding

## Test Order

1. Lock the current baseline and compare every test against it.
2. Add minimal timing instrumentation around:
   - foreground frame loads
   - `grBeginFrame()`
   - `grRestoreBgTiles()`
   - `fgBlit16ToBackgroundRect()`
   - `grUpdateDisplay()` / `grDrawBackground()`
3. Determine whether the slowdown is dominated by:
   - CD reads
   - software compositing
   - background tile upload/present
   - frame cadence logic
4. Test the safest render-cost reduction first:
   - reduce background restore/draw cost without changing frame semantics
5. If needed, test a safer absolute blit path only after instrumentation proves
   the compositor/upload path is the bottleneck.
6. If rendering becomes fast enough but the scene still feels slow, treat the
   remaining problem as timing semantics rather than throughput.
7. Only after foreground timing matches PC should the background/island layer be
   reintroduced.

## Current Interpretation

- Canonical frame count is no longer the main suspect.
- The foreground pilot likely loses speed in the PS1 render/present path rather
  than in random scene selection or export cadence.
- The next step is measurement on the stable path, not more speculative pack
  format changes.
