# Current PS1 Prerender Pilot Status

> **⚠️ Historical snapshot — not current truth.**
> Dated 2026-04-16. Preserved as a prerender-pilot-era status surface
> between the restore-pilot era and the fgpilot/full-SFX baseline. For
> current status see [../scene-status.md](../scene-status.md) and
> [../current-status.md](../current-status.md).

Date: 2026-04-16

## Validated now

- Title screen is restored.
- Default validation flow is `title -> fgpilot fishing1`.
- `fishing1` full-scene foreground playback is visually correct on the black
  base.
- Timing is close to PC after host-deadline replay and deadline catch-up work.
- Generic foreground runtime support is proven on `fishing2`.

## Current blocker

- restoring the real ocean layer under prerender playback without regressing the
  working foreground path

## Failed recent experiments

- naive ocean background swaps inside the standalone prerender loop
  Result:
  repeated Johnny overpaint / ghosting.

- routing `fgpilot fishing1` through full `adsInitIsland() + adsPlay()`
  Result:
  startup behavior changed too much, ocean half-loaded, Johnny never appeared.

## Current conclusion

- the standalone prerender playback path is still the correct foreground pilot
  baseline
- ocean must be restored through a narrower background-only path, not by
  switching `fgpilot` to full ADS scene execution
- the next good milestone is:
  `ocean-only base works`
  then
  `ocean + standalone foreground pack works`

## Working reference command

- `./scripts/rebuild-and-let-run.sh fgpilot fishing1`

## Guardrails

1. One change at a time.
2. Run after every step.
3. Human validation before commit.
4. Keep fixes generic.
