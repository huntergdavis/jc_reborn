# PS1 Port — Current Status

**Last updated:** 2026-04-22 (release `v0.3.6-ps1`, commit `f2737253`).

## Overall

The game boots on DuckStation, loads resources from CD, and runs scene
animations. `FISHING 1` is the first and (for now) only scene that has
been validated under the project's current acceptance bar: pixel-perfect
visuals plus synced SFX, across every applicable variant (night /
low-tide / holiday / raft-stage), signed off by human visual + audible
review.

| Component | Status |
|---|---|
| Build system (Docker + CMake + mkpsxiso) | Complete |
| CD-ROM I/O (`cdrom_ps1.c`) | Complete |
| Graphics layer (`graphics_ps1.c`) | Complete |
| Input layer (`events_ps1.c`) | Complete |
| Resource system (hashed + LRU) | Complete |
| Scene playback (fgpilot, `foreground_pilot.c`) | Primary render path; 1/63 scenes fully validated |
| Audio layer (`sound_ps1.c`) | Working — VAG preload at boot + round-robin SPU voices + captured SFX replay |
| Telemetry / debug overlay | Complete |

## Scenes: 1 / 63 fully validated

The per-scene ledger lives in [scene-status.md](scene-status.md). That
file is the source of truth for what is complete under the current bar;
this page gives the narrative around it.

Milestone release:
- `v0.3.6-ps1` (commit `f2737253`) — fishing1 pixel-perfect with full SFX
  across all variants.
- Prior visual-only release `v0.3.5-ps1` (commit `9448d49f`) —
  superseded by the full-SFX release above.

Release cadence from here: cut every 10 scenes that reach the same
validated bar.

## Primary render methodology: hybrid scene playback (fgpilot)

Desktop host = authoritative renderer and capture source. PS1 = hybrid
replay target. For a validated scene, the PS1 runtime does not
reconstruct the full TTM/ADS scene graph; it replays host-captured
foreground pixels plus captured SFX events and owns only the narrow
runtime surface (background, waves, holiday overlay, SPU playback,
input).

The internal code name for this path is `fgpilot`; public / operator
documentation is migrating to the name **PS1 scene playback**. See
[ps1-branch-cleanup-plan.yaml](ps1-branch-cleanup-plan.yaml) §
`fgpilot_naming_migration_plan`.

### Pipeline

```
desktop host ──► capture-host-scene.sh ──► frames + frame-meta JSONs + sound-events.jsonl
                                  │
                                  ▼
            export-scene-foreground-pilot.sh
                                  │
                                  ▼
            build-scene-foreground-pack.py
                                  │
                                  ▼   (FGP v2: visuals + sound-event table)
               generated/ps1/foreground/*.FG1 ──► CD image ──► PS1
                                                                │
                                                                ▼
                                              foreground_pilot.c (replay)
                                                                │
                                                                ▼
                                             sound_ps1.c soundPlay() on cue
```

### Acceptance model

Primary gate is **human visual + audible signoff** on the scene-playback
path. Regtest, binary-library scans, and harness-based validation are
preserved as secondary / historical tooling — useful for targeted
questions, not for certifying a scene as done. See
[TESTING.md](TESTING.md).

## Audio

SPU is initialised at boot; all 23 VAG sound effects are preloaded into
SPU RAM; `soundPlay(nb)` drives a round-robin over 8 voices. Captured
`0xC051 PLAY_SAMPLE` events from the host TTM interpreter ship in the
FG1 pack (v2) and are fired from `foreground_pilot.c` during replay,
with a 3-frame delay constant to align key-on with the visible frame.

The VAG encoder (`scripts/wav2vag.py`) and the SPU upload/playback path
(`sound_ps1.c`) were extensively debugged during the `v0.3.6-ps1`
milestone; see commit `355227fa` for the bug list (shift-exponent
inversion, ADPCM nibble-pair order, SPU DMA 64-byte alignment,
ADSR1 attack-rate orientation, etc).

## Historical status numbers (not current)

Older validation models were reset by the plan documented in
`ps1-branch-cleanup-plan.yaml` §
`historical_status_surfaces_and_meanings`. They are preserved here for
searchability — **do not cite them as current progress**:

| Count | Date | Meaning at the time | Source |
|---|---|---|---|
| 25 / 63 | 2026-03-21 | Restore-rollout verified scenes | `docs/ps1/project-history.md`, older `current-status.md` |
| 27 / 63 | 2026-03-21 | Research-snapshot verified | `docs/ps1/research/CURRENT_STATUS_2026-03-21.md` |
| 63 / 63 | 2026-03-29 | Harness-level validation claim | `docs/ps1/research/VALIDATION_LOG_2026-03-29.md` — *retroactively demoted as a false summit* |
| 57 / 63 | 2026-04-04 | Scenes rendering with island content | `docs/ps1/TESTING.md` (older) |
| 60 / 63 | 2026-04-04..07 | Bringup in the headless regtest surface | `docs/ps1/TESTING.md` (older), `config/ps1/regtest-scenes.txt` |
| **1 / 63** | **2026-04-22** | **Human-signed reference scene under the full visual + SFX bar** | **this doc, `scene-status.md`** |

Each older count belongs to a different definition of "verified";
they are not comparable to each other or to today's number. The current
scene ledger starts clean from 1 / 63 because that is the only surface
that matches the present proven baseline.

## Build size

| | |
|---|---|
| PS-EXE (`jcreborn.exe`) | ~158 KB |
| CD image (`jcreborn.bin`) | ~46 MB |

## Known limitations

- `printf()` during the PS1 game loop is unsafe — use the telemetry
  overlay (`ps1_debug.c`) for runtime visibility.
- Scene coverage beyond FISHING 1 is pending scene-by-scene bring-up
  via the loop in [development-workflow.md](development-workflow.md).

## See also

- [scene-status.md](scene-status.md) — per-scene ledger
- [development-workflow.md](development-workflow.md) — bring-up loop
- [TESTING.md](TESTING.md) — validation strategy
- [hardware-specs.md](hardware-specs.md)
- [project-history.md](project-history.md) — development narrative
- [research/README.md](research/README.md) — design logs (historical)
- [ps1-branch-cleanup-plan.yaml](ps1-branch-cleanup-plan.yaml) — in-flight cleanup contract
