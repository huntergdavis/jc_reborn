# PS1 Port TODO

Active task list for the PS1 port. Items roughly ordered by priority.

## In Progress

- [ ] Scene coverage expansion (25/63 verified → target all 63)
- [ ] ACTIVITY.ADS tag 4 — fix stale extra-Johnny climb frame
- [ ] BUILDING.ADS / FISHING.ADS — need trustworthy entry paths

## Pause Menu (stubbed, not wired up)

The pause menu code exists (`pause_menu.c/h`) but START button doesn't trigger it.
The input handling in `events_ps1.c` needs debugging — the menu init/show/update
loop may not be wired correctly into the game's frame loop.

- [ ] Debug why START doesn't open pause menu (check events_ps1.c integration)
- [ ] Verify FntLoad/FntPrint text rendering works on PS1
- [ ] Wire up all menu items to their feature backends:
  - [ ] Sound toggle (soundMuteToggle — already implemented)
  - [ ] Telemetry toggle (grSetPs1Telemetry — already implemented)
  - [ ] Captions toggle (captionsSetEnabled — implemented but not displayed)
  - [ ] Scene order toggle (ps1ToggleSceneOrder — implemented)
  - [ ] Direct control toggle (ps1DirectControlEnabled — implemented)
  - [ ] Next scene (ps1SkipToNextScene — implemented)
  - [ ] Set time/date (ps1Soft* globals — implemented)
  - [ ] Scene info display
  - [ ] Controls help display

## Audio

- [x] SPU initialization and VAG loading from CD
- [x] ADSR fix (sustain rate was causing instant fade)
- [x] L1+SELECT mute toggle
- [ ] Verify sounds actually play audibly during scenes
- [ ] Audio description stubs (ps1_accessibility.c — needs pre-recorded VAG narration)
- [ ] Ambient ocean sound (needs looping wave sample)

## Testing Infrastructure

- [x] Headless regtest harness (duckstation-regtest in Docker)
- [x] Visual scene detection engine (visual_detect.py)
- [x] PS1 test instrumentation (ps1_test.h, -DPS1_TEST_BUILD)
- [x] Reference frame capture tooling
- [ ] Run full reference frame capture for all 63 scenes
- [ ] Calibrate visual detection from reference frames
- [ ] Integrate visual detection into regtest-all-scenes.sh
- [ ] Set up baseline hashes for regression detection

## Scene Restore System

- [ ] Close remaining tail bugs on live families
- [ ] PSB/BMP hot path reconciliation
- [ ] Remove replay continuity as correctness dependency (family by family)
- [ ] Promote remaining scenes via cluster contracts

## Performance

- [x] Dirty-rect system (~80-95% data movement reduction)
- [x] Palette LUT + 4-pixel unrolled compositing
- [x] Hash-based O(1) resource lookups
- [x] Compositing function merge (Fwd+Flip)
- [x] Static buffers (no malloc in hot path)
- [ ] Profile actual frame times on hardware/emulator
- [ ] Measure dirty-rect effectiveness per scene

## Polish

- [ ] Caption display rendering (text at bottom of screen during scenes)
- [ ] Scene switching via controller (X / R1 during gameplay)
- [ ] Random vs sequential scene ordering
- [ ] Direct control mode (d-pad moves Johnny)
- [ ] Holiday scene testing (set time/date)
- [ ] Real hardware testing (original PS1)
