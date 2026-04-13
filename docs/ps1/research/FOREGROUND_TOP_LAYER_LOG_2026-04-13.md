# PS1 Foreground Top-Layer Validation Log — 2026-04-13

## Purpose

Track prerendered foreground-only pilot status separately from the normal PS1
scene verification program.

This log does not mean a scene is fully PS1-verified. A scene should only move
into the normal verified bucket after full-scene behavior, timing, and visual
parity are signed off.

## Current Proven Pilot

- `FISHING 1` / `FISHING.ADS tag 1` / scene `17`
  - status: `top-layer verified`
  - proof: boots through `fgpilot fishing1` on PS1 ISO in DuckStation and shows
    the prerendered Johnny/pole/starfish foreground path correctly
  - current asset: full-frame foreground pack (`frame_step = 1`, `156` source
    frames, `156` packed frames)
  - current limitation: playback is visually correct but runs at about half
    speed; background/ocean layer is not yet restored underneath

## Status Legend

- `top-layer verified` — prerendered foreground path is visibly working on PS1
- `export-ready untested` — canonical scene exists, but no PS1 foreground pilot
  validation has been recorded yet

## Scene Status

### ACTIVITY.ADS

- `ACTIVITY 1` / tag `1` / scene `0` — `export-ready untested`
- `ACTIVITY 4` / tag `4` / scene `4` — `export-ready untested`
- `ACTIVITY 5` / tag `5` / scene `5` — `export-ready untested`
- `ACTIVITY 6` / tag `6` / scene `6` — `export-ready untested`
- `ACTIVITY 7` / tag `7` / scene `7` — `export-ready untested`
- `ACTIVITY 8` / tag `8` / scene `8` — `export-ready untested`
- `ACTIVITY 9` / tag `9` / scene `9` — `export-ready untested`
- `ACTIVITY 10` / tag `10` / scene `3` — `export-ready untested`
- `ACTIVITY 11` / tag `11` / scene `2` — `export-ready untested`
- `ACTIVITY 12` / tag `12` / scene `1` — `export-ready untested`

### BUILDING.ADS

- `BUILDING 1` / tag `1` / scene `10` — `export-ready untested`
- `BUILDING 2` / tag `2` / scene `13` — `export-ready untested`
- `BUILDING 3` / tag `3` / scene `12` — `export-ready untested`
- `BUILDING 4` / tag `4` / scene `11` — `export-ready untested`
- `BUILDING 5` / tag `5` / scene `14` — `export-ready untested`
- `BUILDING 6` / tag `6` / scene `16` — `export-ready untested`
- `BUILDING 7` / tag `7` / scene `15` — `export-ready untested`

### FISHING.ADS

- `FISHING 1` / tag `1` / scene `17` — `top-layer verified`
- `FISHING 2` / tag `2` / scene `18` — `export-ready untested`
- `FISHING 3` / tag `3` / scene `19` — `export-ready untested`
- `FISHING 4` / tag `4` / scene `20` — `export-ready untested`
- `FISHING 5` / tag `5` / scene `21` — `export-ready untested`
- `FISHING 6` / tag `6` / scene `22` — `export-ready untested`
- `FISHING 7` / tag `7` / scene `23` — `export-ready untested`
- `FISHING 8` / tag `8` / scene `24` — `export-ready untested`

### JOHNNY.ADS

- `JOHNNY 1` / tag `1` / scene `25` — `export-ready untested`
- `JOHNNY 2` / tag `2` / scene `26` — `export-ready untested`
- `JOHNNY 3` / tag `3` / scene `27` — `export-ready untested`
- `JOHNNY 4` / tag `4` / scene `28` — `export-ready untested`
- `JOHNNY 5` / tag `5` / scene `29` — `export-ready untested`
- `JOHNNY 6` / tag `6` / scene `30` — `export-ready untested`

### MARY.ADS

- `MARY 1` / tag `1` / scene `31` — `export-ready untested`
- `MARY 2` / tag `2` / scene `33` — `export-ready untested`
- `MARY 3` / tag `3` / scene `32` — `export-ready untested`
- `MARY 4` / tag `4` / scene `34` — `export-ready untested`
- `MARY 5` / tag `5` / scene `35` — `export-ready untested`

### MISCGAG.ADS

- `MISCGAG 1` / tag `1` / scene `36` — `export-ready untested`
- `MISCGAG 2` / tag `2` / scene `37` — `export-ready untested`

### STAND.ADS

- `STAND 1` / tag `1` / scene `38` — `export-ready untested`
- `STAND 2` / tag `2` / scene `39` — `export-ready untested`
- `STAND 3` / tag `3` / scene `40` — `export-ready untested`
- `STAND 4` / tag `4` / scene `41` — `export-ready untested`
- `STAND 5` / tag `5` / scene `42` — `export-ready untested`
- `STAND 6` / tag `6` / scene `43` — `export-ready untested`
- `STAND 7` / tag `7` / scene `44` — `export-ready untested`
- `STAND 8` / tag `8` / scene `45` — `export-ready untested`
- `STAND 9` / tag `9` / scene `46` — `export-ready untested`
- `STAND 10` / tag `10` / scene `47` — `export-ready untested`
- `STAND 11` / tag `11` / scene `48` — `export-ready untested`
- `STAND 12` / tag `12` / scene `49` — `export-ready untested`
- `STAND 15` / tag `15` / scene `50` — `export-ready untested`
- `STAND 16` / tag `16` / scene `51` — `export-ready untested`

### SUZY.ADS

- `SUZY 1` / tag `1` / scene `52` — `export-ready untested`
- `SUZY 2` / tag `2` / scene `53` — `export-ready untested`

### VISITOR.ADS

- `VISITOR 1` / tag `1` / scene `54` — `export-ready untested`
- `VISITOR 3` / tag `3` / scene `55` — `export-ready untested`
- `VISITOR 4` / tag `4` / scene `56` — `export-ready untested`
- `VISITOR 5` / tag `5` / scene `59` — `export-ready untested`
- `VISITOR 6` / tag `6` / scene `57` — `export-ready untested`
- `VISITOR 7` / tag `7` / scene `58` — `export-ready untested`

### WALKSTUF.ADS

- `WALKSTUF 1` / tag `1` / scene `60` — `export-ready untested`
- `WALKSTUF 2` / tag `2` / scene `61` — `export-ready untested`
- `WALKSTUF 3` / tag `3` / scene `62` — `export-ready untested`

## References

- scene catalog: [regtest-references/manifest.csv](/home/hunter/workspace/jc_reborn/regtest-references/manifest.csv)
- pilot rationale: [OFFLINE_SCENE_PLAYBACK_PIVOT_2026-04-12.md](/home/hunter/workspace/jc_reborn/docs/ps1/research/OFFLINE_SCENE_PLAYBACK_PIVOT_2026-04-12.md)
