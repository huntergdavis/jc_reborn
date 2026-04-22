# PS1 Port Testing Guide

**Primary acceptance = human visual + audible signoff** on the
scene-playback (fgpilot) path. Everything else in this document is
secondary tooling: useful for targeted questions, not for certifying a
scene as done.

For the scene-by-scene bring-up loop, see
[development-workflow.md](development-workflow.md). For the per-scene
ledger, see [scene-status.md](scene-status.md).

## Primary: scene-playback validation

```bash
./scripts/rebuild-and-let-run.sh noclean
```

Builds the PS1 executable + CD image inside Docker and launches
DuckStation with the cue. `BOOTMODE.TXT` controls which scene and which
variant boots. A scene reaches `✅ / ✅` in `scene-status.md` when a
human has confirmed visuals + SFX are correct across every applicable
variant for that scene. Release cadence: every 10 such scenes.

### Variant tokens

| Token | Range | Purpose |
|---|---|---|
| `night <0\|1>` | 0 or 1 | Night/dusk palette |
| `lowtide <0\|1>` | 0 or 1 | Tide state |
| `holiday <N>` | 0..4 | Holiday overlay variant |
| `raft-stage <N>` | 0..5 | Cumulative raft-build state |
| `island-pos <x> <y>` | — | Force island position |
| `story-day <N>` | 1..11 | Force story day |

## Secondary (historical): headless regtest harness

The headless regtest harness runs DuckStation's regtest binary in Docker
and captures per-frame data. It's preserved for targeted diagnostics
(regression hunts, deterministic frame-timing investigations) but is not
the certification gate.

```bash
# Single-scene run
./scripts/regtest-scene.sh --scene "STAND 2"

# Longer run with a custom interval
./scripts/regtest-scene.sh --scene "BUILDING 1" --frames 9000 --interval 120

# Raw headless run with a late capture window
./scripts/run-regtest.sh --frames 3600 --start-frame 2400 --dumpinterval 60 --dumpdir scratch/regtest-out
```

Scene manifest: `config/ps1/regtest-scenes.txt`. The `verified`,
`bringup`, `blocked`, `untested` tokens in that file reflect the legacy
regtest-route status model, **not** the current scene-playback bar. See
`ps1-branch-cleanup-plan.yaml` § `status_model_correction`.

Regtest output:
```
regtest-results/<ads>-<tag>/
  frames/*.png              # captured screenshots
  telemetry.json            # debug-panel data
  result.json               # structured outcome
  printf.log                # PS1 TTY output (if PCDrv enabled)
```

## Secondary (historical): binary library

The binary-library stack builds a PS1 executable + CD image for every
code-changing commit, enabling regression bisection against any historical
build. Useful for archaeology; retired from the primary workflow.

```bash
# Full library (~15 min, ~118 GB — see cleanup plan)
./scripts/build-binary-library.sh
```

`ps1-branch-cleanup-plan.yaml` plans to archive `binary-library/` out of
the repo and retain only a manifest + sample-rebuild path. See plan
phase `phase_04_retire_binary_library_surface`.

## Boot modes

The PS1 executable reads `BOOTMODE.TXT` from the CD. Primary mode for
current development:

| Mode | Example | Description |
|---|---|---|
| `fgpilot <slug> [tokens...]` | `fgpilot fishing1 night 1` | **Primary.** Hybrid scene playback from the FG1 pack, with variant tokens. |

Secondary / legacy routes (retained for diagnostics):

| Mode | Example | Description |
|---|---|---|
| `story scene N` | `story scene 38` | Play scene via story loop (printf crash risk) |
| `story direct N` | `story direct 25` | Play scene directly, bypass story loop |
| `island ads X.ADS N` | `island ads BUILDING.ADS 1` | Play ADS tag directly with island background |

## Known runtime pitfalls

- **`printf()` is unsafe in the PS1 game loop.** PS1 `printf()` uses the
  BIOS break instruction for TTY output and crashes the game when called
  after init. Use the telemetry overlay (`ps1_debug.c`) instead.
  Affected historical paths: `fprintf(stderr, …)` macros,
  `grCaptureEmitFrameMetadataLine`, `fatalError`.
- **Cold-boot ADS scenes** (`FISHING 1`, `FISHING 2`, `FISHING 6`) have
  `ADD_SCENE` commands behind `IF_LASTPLAYED`. On cold boot these
  conditions are never satisfied, producing an empty scene. The
  fgpilot path sidesteps this by replaying captured frames directly.
- **`/tmp` is off-limits for scratch files.** Redirect long-running
  output (DuckStation logs, capture bundles) to `scratch/` in the repo.
  `/tmp` has filled and broken the shell before.

## Docker images

| Image | Purpose | Build |
|---|---|---|
| `jc-reborn-ps1-dev:amd64` | PS1 cross-compile (PSn00bSDK) | `./scripts/build-docker-image.sh` |
| `jc-reborn-regtest:latest` | Headless DuckStation regtest | `config/ps1/Dockerfile.regtest` |

## Do not cite

The older `Scene Status (as of 2026-04-04)` table (60 verified / 3
blocked / 63 total) from earlier versions of this file reflects the
legacy regtest-route status model and does not translate to the current
acceptance bar. It is preserved as history in
`current-status.md` § "Historical status numbers (not current)". Current
per-scene status lives in [scene-status.md](scene-status.md).
