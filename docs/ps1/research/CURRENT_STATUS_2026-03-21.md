# Current PS1 Restore Rollout Status

> **⚠️ Historical snapshot — not current truth.**
> Dated 2026-03-21. Preserved as the restore-pilot-era status surface.
> The "verified" counts below belong to the restore-rollout validation
> model and are not comparable to the current scene-playback bar. For
> current status see [../scene-status.md](../scene-status.md) and
> [../current-status.md](../current-status.md).

Date: 2026-03-21

- verified scenes: `27 / 63`
- live header scenes: `27 / 63`
- live bring-up scenes not yet counted as verified: `0`

## Verified

- `STAND.ADS` tags `[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 15, 16]` (14 scenes)
- `JOHNNY.ADS` tags `[1, 2, 3, 4, 5, 6]` (6 scenes)
- `WALKSTUF.ADS` tags `[1, 2, 3]` (3 scenes)
- `MISCGAG.ADS` tags `[1, 2]` (2 scenes)
- `MARY.ADS` tags `[3, 5]` (2 scenes)

## Bring-Up

- No scenes are currently in the live-header bring-up bucket. `ACTIVITY.ADS tag 4` remains a runtime issue, but it is not promoted in the current live header.

## Blocked / Unreliable Entry Paths

- `BUILDING.ADS` still needs a trustworthy entry/validation path before promotion.
- `FISHING.ADS` tag `1` still needs a trustworthy entry/validation path before promotion.

## Notes

- This status snapshot supersedes the archived 2026-03-19 rollout manifest for day-to-day planning.
- The generated header currently includes 27 promoted scene tags, all counted as verified.
- `MARY.ADS` tags `3` and `5` are now verified live under the retained `MARY`-specific startup and restore-hook policy.
- The current pack-side artifact surface was refreshed on 2026-03-21:
  `10` family packs, `560` compiled entries, `219` PSB entries, and
  `22,743,040` bytes of compiled research payload under
  [generated/compiled_packs_2026-03-21](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/compiled_packs_2026-03-21).
