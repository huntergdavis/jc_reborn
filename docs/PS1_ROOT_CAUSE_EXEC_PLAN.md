# PS1 Root Cause Execution Plan

Date: 2026-03-04
Owner: Port debugging

## Scope Split (Authoritative)

There are **two separate bugs** and they must be debugged independently:

1. **Primary bug (A): In-scene multi-sprite dropout**
- Symptom: Johnny body parts/full sprite disappear **during active scenes** when extra sprites are present (fish/crab/note/etc).
- This is the main blocker.

2. **Secondary bug (B): Transition flicker**
- Symptom: Johnny flickers/blinks between walking/scene handoff and major transitions.
- Important but separate from (A).

Do not merge conclusions across A/B unless telemetry proves shared causality.

## Current Facts

- Runs can freeze early; freezes often happen after scene-end or during busy sprite windows.
- Telemetry stack and screenshot decoder are in tree:
  - `docs/PS1_LEFT_DEBUG_PANEL.md`
  - `scripts/decode-ps1-bars.py`
- Latest signal indicates replay-validity failures exist, but we have not yet isolated producer path per bug class.

## Success Criteria

### A: Multi-sprite dropout fixed
- In fish/crab/note scenes, no Johnny disappearance while extra sprites are visible.
- `ads_replay_reject_slot`, `ads_replay_reject_sprite` remain near-zero during these scenes.
- 10+ minute run with repeated multi-sprite scenes without body-part loss.

### B: Transition flicker reduced/fixed
- No visible blink during walk->scene handoff for at least 30 consecutive transitions.

## Execution Plan

### Phase 1: Lock Baseline and Collect A-specific Evidence
1. Run current build with telemetry enabled.
2. Capture **paired screenshots** in same scene:
   - frame where Johnny visible with extra sprite present
   - frame where Johnny disappears while extra sprite still present
3. Decode both with:
   - `scripts/decode-ps1-bars.py <shot>`
4. Record diffs in `docs/PS1_DEBUGGING.md` under new section “A/B Evidence Log”.

### Phase 2: Add Targeted Telemetry for A (in-scene only)
Add counters that answer exactly this:
- Were Johnny draw records dropped/rejected because:
  - slot generation changed mid-scene?
  - sprite pointer became null after release?
  - record list overwritten by non-Johnny draws?

Implementation target:
- Add per-frame + per-scene rows for:
  - `replay_reject_slotgen_midscene`
  - `replay_reject_sprite_null_midscene`
  - `johnny_draw_records_frame`
  - `non_johnny_draw_records_frame`
- Keep bars permanent and update `docs/PS1_LEFT_DEBUG_PANEL.md`.

### Phase 3: Fix A with Minimal Mutation
Apply one fix at a time and re-test:
1. Preserve Johnny records across transient invalid windows without clearing entire list.
2. Prevent slot reuse from invalidating active-scene Johnny records before replacement is ready.
3. If needed, separate replay channels:
   - actor replay list (Johnny)
   - auxiliary replay list (fish/crab/note/background actors)

Validation after each change:
- same paired screenshot method
- decoded bar comparison
- 10 minute long run

### Phase 4: Address B Separately
Only after A is stable:
1. Use transition-only captures (walk end / scene start).
2. Analyze flicker bars and transition carry state.
3. Fix walk/scene handoff ordering without touching A’s replay lifetimes.

## Guardrails

- No broad “watchdog mutations” that reset live thread state unless explicitly A/B-scoped and proven safe.
- No mixing A/B data in a single conclusion.
- Every telemetry addition must be documented in `docs/PS1_LEFT_DEBUG_PANEL.md`.

## Quick Commands

```bash
# Run build
./scripts/rebuild-and-let-run.sh noclean

# Decode one screenshot
scripts/decode-ps1-bars.py "/path/to/screenshot.png"

# Decode to JSON for diff tooling
scripts/decode-ps1-bars.py --json "/path/to/screenshot.png"
```

## Immediate Next Action

Start Phase 1 and gather one **visible vs missing** screenshot pair from the same multi-sprite scene (fish/crab/note), decode both, then implement Phase 2 telemetry additions.

## Execution Status (2026-03-04)

- Completed:
  - Phase 2 telemetry additions implemented (midscene reject split + draw-record pressure counters).
  - Phase 3 step-1 mutation implemented: replay-record dedupe and non-overwriting overflow handling.
  - Left-panel docs and decoder script updated for new Track-A rows.
  - Rebuild/run completed via `./scripts/rebuild-and-let-run.sh noclean`.

- Pending:
  - Collect a fresh same-scene visible/missing screenshot pair from the new build.
  - Decode and compare Track-A rows to confirm whether body-loss correlates with draw-record overflow versus reject path.
  - If overflows remain non-zero during blink frames, proceed to Phase 3 step-2 (slot reuse hardening).
