# Ocean Restore Plan 2026-04-16

Status: active short-term plan
Owner: PS1 prerender pilot

## Safe baseline

- `title -> fgpilot fishing1` is the current known-good path.
- `fishing1` prerender playback is visually correct on the black base.
- Timing is close to PC after host-deadline replay and deadline catch-up work.
- The generic foreground runtime path has already been proven on `fishing2`.

Reference launch:

- `./scripts/rebuild-and-let-run.sh fgpilot fishing1`

## What just failed

These are explicitly not the next step:

- swapping ocean into the standalone prerender loop by changing only the base
  background load
- routing `fgpilot fishing1` through full `adsInitIsland() + adsPlay()`

Observed failures:

- repeated Johnny overpaint / ghosting when ocean was forced under the
  standalone pack player
- partial scene startup and missing Johnny when full ADS scene routing was used

Conclusion:

- ocean cannot be restored safely by brute-forcing a different background into
  the current standalone playback loop
- full ADS scene routing changes too much behavior at once for this milestone

## Goal

Restore the real ocean layer under prerender playback while keeping the
foreground pack playback path stable and generic.

## Execution rules

1. One change at a time.
2. Run after every step.
3. User validates every step before any commit.
4. No Fishing-1-only runtime hacks.
5. Do not reintroduce full ADS scene playback as an implicit side effect.

## Step sequence

### Step 1: Lock baseline again

Goal:

- keep `title -> fgpilot fishing1` on the known-good black-base path

Exit criteria:

- visuals match the current baseline
- no ocean yet
- no Johnny ghosting

### Step 2: Prove ocean-only initialization in isolation

Goal:

- initialize the ocean/island background machinery without starting scene ADS
  logic or foreground playback

Requirements:

- no walk thread
- no island animation thread unless explicitly needed for the ocean base to
  appear
- no foreground prerender overlay yet

Exit criteria:

- ocean appears correctly
- no corruption or title leftovers
- stable for a simple hold run

### Step 3: Put the existing standalone foreground player on top of the proven
### ocean-only base

Goal:

- reuse the exact current standalone `fishing1` foreground playback loop
- change only the source of the clean background baseline

Exit criteria:

- ocean visible
- Johnny and props still render correctly
- no cumulative overpaint / ghosting

### Step 4: If Step 3 fails, fix restore semantics before trying more init work

Primary suspects:

- `grRestoreBackgroundRectForFrame()`
- `grRestoreAndCompositeDirect16BackgroundRectForFrame()`
- `prevDirty` / `currDirty` advancement
- assumptions in the standalone loop that only happen to work on black

Rule:

- do not add more background setup complexity until restore correctness is
  proven against a non-black clean baseline

### Step 5: Only after ocean is stable, restore island

Goal:

- static island first
- no random placement yet

### Step 6: Only after static island is stable, restore random island placement

Goal:

- generic island positioning
- foreground playback still composes correctly

## Immediate next move

Build an `ocean-only` proof path that initializes the real ocean background
without entering full ADS scene playback, validate that by itself, then layer
the current standalone `fishing1` foreground playback over that same base.
