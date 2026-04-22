# Johnny Reborn — PlayStation 1 Port

Quick-start entrypoint for the PS1 branch. The primary render path is
**hybrid scene playback** (internal code name `fgpilot`): host-captured
foreground pixels, captured SFX, and a narrow PS1 runtime that handles
background, waves, holiday overlay, and SFX playback.

## Current status

| | |
|---|---|
| Release | `v0.3.6-ps1` (commit `f2737253`) |
| Reference scene | `FISHING 1` — pixel-perfect visuals + synced SFX across night / low-tide / holiday / raft-stage |
| Scenes fully validated under the reference bar | **1 / 63** |
| Full ledger | [scene-status.md](scene-status.md) |

"Fully validated" means human visual + audible signoff on the scene-playback
path. Older counts (`25/63`, `60/63`, etc.) belong to earlier validation
models and are preserved as history in `current-status.md`, not carried
forward as current progress.

## Quick start

### Prerequisites
- Docker (for the `jc-reborn-ps1-dev:amd64` build image + PSn00bSDK)
- DuckStation (Flatpak: `org.duckstation.DuckStation`)

### Build + run the reference scene
```bash
./scripts/rebuild-and-let-run.sh noclean
```
Builds the PS1 executable, generates `jcreborn.bin/.cue`, launches
DuckStation pointed at the cue. Boots into `FISHING 1` via `BOOTMODE.TXT`
(`fgpilot fishing1`).

### Validate a variant
```bash
# night / low-tide / raft-stage / holiday
./scripts/rebuild-and-let-run.sh noclean fgpilot fishing1 night 1
./scripts/rebuild-and-let-run.sh noclean fgpilot fishing1 lowtide 1
./scripts/rebuild-and-let-run.sh noclean fgpilot fishing1 raft-stage 5
./scripts/rebuild-and-let-run.sh noclean fgpilot fishing1 holiday 4
```

### Bring up a new scene
See [development-workflow.md](development-workflow.md) for the full
capture → pack → validate loop. High level:
1. `./scripts/export-scene-foreground-pilot.sh` — host capture → FG1 pack + sound-event JSONL.
2. Add the pack entry to `config/ps1/cd_layout.xml`.
3. Add scene routing in `foreground_pilot.c`.
4. Rebuild ISO, launch via `rebuild-and-let-run.sh`, iterate to pixel-perfect.
5. Tick the row in `scene-status.md`.

## Why PS1?

Johnny Reborn's tight memory footprint and native 640×480 target fit PS1
closely:
- **Main RAM**: 2 MB
- **VRAM**: 1 MB
- **SPU RAM**: 512 KB (holds all 23 SFX VAGs preloaded at boot)
- **Native output**: 640×480 interlaced

The port is deliberately **hybrid**: the desktop host is the authoritative
renderer and capture source; PS1 replays captured foreground + SFX and
owns only the narrow runtime surface (background, wave animation,
holiday overlay, SPU playback, input).

### Architecture

**Unchanged from the desktop engine:**
- Core engine (`ttm.c`, `ads.c`, `story.c`)
- Game logic (`walk.c`, `calcpath.c`, `island.c`)
- Utilities (`utils.c`, `config.c`, `bench.c`)

**PS1-specific:**
- `graphics_ps1.c` — PSn00bSDK GPU + software compositing
- `sound_ps1.c` — SPU playback (VAG preload at boot + round-robin voices)
- `events_ps1.c` — PSX controller input
- `cdrom_ps1.c` — CD-ROM file I/O
- `foreground_pilot.c` — FG1 pack loader, frame-advance, SFX event firing

**Offline pipeline (`scripts/`):**
- `capture-host-scene.sh` — desktop capture (frames + metadata + sound events)
- `export-scene-foreground-pilot.sh` — wraps capture + pack build for a scene
- `build-scene-foreground-pack.py` — FG1 v2 pack compiler (visuals + SFX)
- `wav2vag.py` — WAV → PS1 SPU ADPCM VAG encoder
- `make-cd-image.sh` / `build-ps1.sh` / `rebuild-and-let-run.sh` — build + launch

## Controller mapping

| PSX Button | Action |
|---|---|
| Start | Pause / Unpause |
| Select | Toggle debug |
| Triangle | Advance frame (paused) |
| Circle | Toggle max speed |
| X / L1 / L2 / R1 / R2 | Reserved |

## Documentation

**Current truth**
- [scene-status.md](scene-status.md) — per-scene ledger under the reference bar
- [current-status.md](current-status.md) — detailed progress + history of earlier validation models
- [development-workflow.md](development-workflow.md) — operator loop for bringing up a new scene
- [TESTING.md](TESTING.md) — validation strategy (primary = human signoff; regtest = legacy)

**Platform reference**
- [hardware-specs.md](hardware-specs.md) — PS1 hardware
- [api-mapping.md](api-mapping.md) — SDL2 → PSn00bSDK mapping
- [build-system.md](build-system.md) — CMake / Docker / CD generation
- [toolchain-setup.md](toolchain-setup.md) — dev environment

**History / archaeology** (kept searchable, not current truth)
- [project-history.md](project-history.md)
- [research/README.md](research/README.md) — design logs and prior status snapshots
- [audio-optimization-spec.md](audio-optimization-spec.md)
- [ps1-branch-cleanup-plan.yaml](ps1-branch-cleanup-plan.yaml) — in-flight cleanup contract

## External references

- [PSn00bSDK](https://github.com/Lameguy64/PSn00bSDK)
- [mkpsxiso](https://github.com/Lameguy64/mkpsxiso)
- [DuckStation](https://github.com/stenzek/duckstation)
- [PS1 Dev Resources](https://psx.arthus.net/)

## License

GPL-3.0, same as main project.
