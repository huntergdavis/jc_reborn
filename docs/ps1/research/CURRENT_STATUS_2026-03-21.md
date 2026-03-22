# Current PS1 Restore Rollout Status

Date: 2026-03-21

- verified scenes: `25 / 63`
- live header scenes: `26 / 63`
- live bring-up scenes not yet counted as verified: `1`

## Verified

- `STAND.ADS` tags `[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 15, 16]` (14 scenes)
- `JOHNNY.ADS` tags `[1, 2, 3, 4, 5, 6]` (6 scenes)
- `WALKSTUF.ADS` tags `[1, 2, 3]` (3 scenes)
- `MISCGAG.ADS` tags `[1, 2]` (2 scenes)

## Bring-Up

- `ACTIVITY.ADS` tags `[4]` are live in the header but not counted as verified yet; current remaining issue is a stale extra-frame artifact during the palm-climb route.

## Blocked / Unreliable Entry Paths

- `BUILDING.ADS` still needs a trustworthy entry/validation path before promotion.
- `FISHING.ADS` tag `1` still needs a trustworthy entry/validation path before promotion.

## Notes

- This status snapshot supersedes the archived 2026-03-19 rollout manifest for day-to-day planning.
- ACTIVITY.ADS tag 4 is intentionally excluded from verified count until the stale extra-Johnny climb frame is fixed.
- The generated header currently includes 26 promoted scene tags: 25 verified plus 1 bring-up scene.
