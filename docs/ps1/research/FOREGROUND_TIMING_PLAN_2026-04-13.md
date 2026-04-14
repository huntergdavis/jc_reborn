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

## Expanded Speed Backlog

This is the chained improvement backlog for getting `fgpilot fishing1` to match
 PC timing while preserving the currently-correct visuals.

1. Specialize the foreground composite path further for prerender playback.
   Remove remaining generic sprite/compositor behavior from the hot path and
   keep it focused on copying a 16-bit rect into background tile RAM.
2. Add row-span clipping once per row, not per pixel.
   Compute visible row bounds once, then copy the exact span with contiguous
   memory moves.
3. Tighten the background-restore rect path further.
   Keep the current narrow restore behavior, but reduce branchy generic logic
   inside the row copy loop.
4. Skip restoring overlap that the next frame fully overwrites.
   Restore only the exposed bands when consecutive frame rects overlap heavily.
5. Add a same-rect fast path.
   When consecutive frames use the same x/y/width/height, reuse the same copy
   plan instead of recomputing clipping and tile intersections.
6. Precompute tile split plans in the pack builder.
   Store which tiles each frame touches and the clipped subrects needed for
   runtime blits.
7. Precompute row offsets for each frame entry.
   Convert runtime per-row pointer math into simple offset table lookups.
8. Align pack payloads more aggressively.
   Use sector-friendly or word-friendly offsets to reduce read and copy churn.
9. Preload the frame entry table once.
   Stop doing tiny per-frame metadata reads if the full entry table can live in
   RAM cheaply.
10. Add bounded frame read-ahead.
    Try a one-frame or two-frame payload buffer so synchronous reads stop
    stalling the render path.
11. Double-buffer frame payload RAM.
    Read or decode frame N+1 while frame N is being presented.
12. Make held frames do no unnecessary work.
    If the current image is already correct, skip restore and composite on hold
    frames and only present/wait.
13. Collapse more duplicate canonical frames in the pack.
    Preserve canonical playback semantics while reducing repeated payload work.
14. Revisit delta/diff encoding only as an offline pack experiment.
    Do not put the broken live direct-diff path back into the runtime.
15. Add a black-background pilot specialization.
    Since the current proof uses a black base, bypass background machinery that
    only exists for full scene restoration when safe.
16. Revisit display mode only after CPU-side work is lean.
    `480i` vs progressive may still matter, but only after the stable render
    path is otherwise optimized.
17. Confirm whether CD access is still a bottleneck with a bounded cache.
    Keep within PS1 RAM limits and avoid full-pack preload.
18. Rebuild pack payload layout for runtime-friendly row access.
    Prefer payload organization that minimizes pointer churn and branching.
19. Export richer canonical per-frame metadata.
    Include repeat/hold information, rect reuse hints, and identical-to-previous
    flags where useful.
20. Only after timing is right, bring back the real background/island layer.
    Do not mix scene reconstruction with speed debugging before Fishing 1 feels
    correct.

## Recommended Execution Order

1. Preload the frame entry table once.
2. Add pack-time precomputed tile split data.
3. Add a same-rect fast path.
4. Add overlap-aware restore skipping.
5. Make held frames do no unnecessary work.
6. Add bounded payload read-ahead or double buffering.
7. Reassess display mode only after the CPU/render path is lean.

## Repeat Prompt

Use this prompt to continue the speed work in the intended way:

> Continue PS1 Fishing 1 foreground speed improvements from the current repo state. Stay on the stable `fgpilot fishing1` path only. Make one high-impact safe improvement from `docs/ps1/research/FOREGROUND_TIMING_PLAN_2026-04-13.md`, launch it with `./scripts/rebuild-and-let-run.sh fgpilot fishing1`, and stop for my validation. Do not use experimental direct framebuffer, diff, or progressive paths unless explicitly asked.
