# PS1 Development Workflow

Operator loop for bringing up a new scene to the current acceptance bar
(pixel-perfect visuals + synced SFX across applicable variants). See
[scene-status.md](scene-status.md) for the per-scene ledger.

## Prerequisites

- Docker (or `sudo docker`) with image `jc-reborn-ps1-dev:amd64`
- DuckStation (Flatpak: `org.duckstation.DuckStation`)
- The project's `jc_reborn` host binary built at least once:
  ```bash
  ./scripts/build-host.sh
  ```

## The per-scene loop

One iteration = one scene promoted from `⏳` to `✅ / ✅` in
[scene-status.md](scene-status.md).

### 1. Capture the scene on the desktop host

```bash
./scripts/export-scene-foreground-pilot.sh \
    ''                   \  # output dir (default: host-results/<slug>-foreground-pilot)
    <slug>               \  # e.g. fishing2
    '<ADS TAG>'          \  # e.g. 'FISHING 2'
    <PACK_BASENAME>      \  # e.g. FISHING2
    <raw_frame_idx>      \  # which capture frame to snapshot as the establishing .RAW
    <RAW_BASENAME>          # e.g. FISH
```

Produces:
- `host-results/<slug>-foreground-pilot/host-capture/frames/*.bmp`
- `host-results/<slug>-foreground-pilot/host-capture/frame-meta/*.json`
- `host-results/<slug>-foreground-pilot/host-capture/sound-events.jsonl`
- `generated/ps1/foreground/<PACK_BASENAME>.FG1` (overlay pack, FGP v2)
- `generated/ps1/foreground/<PACK_BASENAME>D.FG1` (delta pack, FGP v2)
- `generated/ps1/foreground/<RAW_BASENAME><idx>.RAW` (establishing frame)

The `host-results/` tree is gitignored; only the `generated/ps1/foreground/*.FG1`
and `.RAW` files are committed.

### 2. Pick the establishing frame

The `.RAW` is a pre-rendered background stamped before the FG1 replay
starts, so the user sees a coherent scene instantly. Inspect
`host-capture/frames/frame_NNNNN.bmp` and pick the first frame where
static content is laid down but animation has not begun. Re-run step 1
with the new `raw_frame_idx` if needed.

### 3. Wire the scene in

`config/ps1/cd_layout.xml` — add:
```xml
<file name="<PACK_BASENAME>.FG1"  type="data" source="../../generated/ps1/foreground/<PACK_BASENAME>.FG1"/>
<file name="<PACK_BASENAME>D.FG1" type="data" source="../../generated/ps1/foreground/<PACK_BASENAME>D.FG1"/>
<file name="<RAW_BASENAME><idx>.RAW" type="data" source="../../generated/ps1/foreground/<RAW_BASENAME><idx>.RAW"/>
```

`foreground_pilot.c` — add the scene to the routing functions:
- `fgOverlayPackPathForScene(sceneName)` → `\\FG\\<PACK_BASENAME>.FG1;1`
- `fgDirectPackPathForScene(sceneName)` → `\\FG\\<PACK_BASENAME>D.FG1;1`
- `fgRawFramePathForScene(sceneName)` → `\\FG\\<RAW_BASENAME><idx>.RAW;1`
- `fgAdsNameForScene(sceneName, &adsTagOut)` → sets the ADS filename + tag

### 4. Build + launch

```bash
./scripts/rebuild-and-let-run.sh noclean
```

Builds the PS1 executable inside Docker, regenerates the CD image, and
launches DuckStation with the cue. BIOS plays first (boot script no
longer passes `-fastboot`, so you get the chime for volume calibration).
The game then boots straight into the scene named in `BOOTMODE.TXT`.

### 5. Validate variants

Edit `config/ps1/BOOTMODE.TXT` (or pass tokens on the `rebuild-and-let-run`
line) to exercise each applicable variant:

```
fgpilot <slug>                          # default
fgpilot <slug> night 1                  # dusk / night palette
fgpilot <slug> lowtide 1                # tide state
fgpilot <slug> holiday <N>              # holiday overlay 1..4
fgpilot <slug> raft-stage <N>           # raft build stage 0..5
fgpilot <slug> island-pos <x> <y>       # forced island position
```

Strike through any variant that does not apply to the scene (see
[scene-status.md](scene-status.md) legend). Sign off each by human
visual + audible review.

### 6. Tick the row and commit

In `docs/ps1/scene-status.md`:
- Update the scene's row: `⏳` → `✅` for visuals and SFX.
- List the validated variants (or strike through N/A ones).
- Fill in "last verified" with the release tag once the next release
  lands, or the current commit SHA in the interim.

Commit with a scene-scoped message:
```
<slug>: pixel-perfect playback with synced SFX
```

### 7. Release cadence

Every **10** scenes reaching `✅ / ✅` under this bar:
```bash
./scripts/release.sh "<milestone message>"
```
which bumps `VERSION`, copies the ISO to `release/`, creates an annotated
tag `vX.Y.Z-ps1`, and pushes.

## Rebuild / launch shortcuts

```bash
# Build PS1 executable only (incremental)
./scripts/build-ps1.sh

# Clean PS1 rebuild
./scripts/build-ps1.sh clean

# Regenerate CD image only (after build)
./scripts/make-cd-image.sh

# Build + CD image + launch DuckStation
./scripts/rebuild-and-let-run.sh noclean

# Full release (build + bump + tag + push)
./scripts/release.sh "<message>"
```

## Debug + diagnostics

- **Telemetry overlay**: `ps1_debug.c` provides a 5-panel on-screen
  overlay. Use this instead of `printf()` — printing during the game
  loop is unsafe on PS1.
- **DuckStation console** output lands in DuckStation's own log.
- **Per-scene capture diffs**: the host-capture frames + frame-meta
  JSONs are a useful ground truth when a replay mismatches. Point
  `compare-scene-reference.py` at them.
- **Keep scratch files in `scratch/`**, never `/tmp`. DuckStation
  logs grow fast and `/tmp` has filled and broken the shell before.

## Historical / secondary tooling

The headless regtest harness (`scripts/regtest-scene.sh`,
`scripts/run-regtest.sh`, `config/ps1/regtest-scenes.txt`) and the
binary-library regression stack are retained as secondary tools for
targeted questions. They are **not** the primary acceptance gate;
refer to [TESTING.md](TESTING.md) for when each is still useful and
[ps1-branch-cleanup-plan.yaml](ps1-branch-cleanup-plan.yaml) for
pending archival decisions.

## See also

- [scene-status.md](scene-status.md) — per-scene ledger
- [current-status.md](current-status.md) — project narrative + history
- [TESTING.md](TESTING.md) — validation strategy
- [build-system.md](build-system.md) — CMake / Docker / CD layout
- [toolchain-setup.md](toolchain-setup.md) — dev environment
