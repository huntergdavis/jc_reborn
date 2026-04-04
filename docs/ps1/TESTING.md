# PS1 Port Testing Guide

## Overview

The PS1 port uses a headless testing harness built on DuckStation's regtest
binary running inside Docker. This enables fully automated, deterministic
scene validation without a display server.

## Quick Start

```bash
# Setup (once, on a fresh machine)
./scripts/setup-dev-environment.sh

# Build and test a single scene
./scripts/regtest-scene.sh --scene "STAND 2"

# Test a specific scene with more frames
./scripts/regtest-scene.sh --scene "BUILDING 1" --frames 9000 --interval 120

# View the result
ls regtest-results/building-1/frames/
```

## Architecture

```
┌────────────────────┐    ┌──────────────────────┐
│  regtest-scene.sh  │───>│   build-ps1.sh       │  Build PS1 executable
│  (orchestrator)    │    │   make-cd-image.sh    │  Create CD image
│                    │───>│   run-regtest.sh      │  Run headless DuckStation
│                    │───>│   decode-ps1-bars.py  │  Extract telemetry
│                    │    └──────────────────────┘
│                    │
│  Output:           │
│  - frames/*.png    │  Captured screenshots at interval
│  - telemetry.json  │  Debug panel data
│  - result.json     │  Structured test outcome
│  - printf.log      │  PS1 TTY output (if PCDrv enabled)
└────────────────────┘
```

## Scene Manifest

All 63 scenes are listed in `config/ps1/regtest-scenes.txt`:

```
# Format: ADS_NAME TAG SCENE_INDEX STATUS BOOTMODE...
STAND 1 38 verified story scene 38
BUILDING 1 10 verified island ads BUILDING.ADS 1
```

**Status values:**
- `verified` — renders correctly in headless regtest
- `bringup` — being worked on
- `blocked` — known issue preventing rendering
- `untested` — not yet tested

## Boot Modes

The PS1 executable accepts boot overrides via `config/ps1/BOOTMODE.TXT`
(embedded at build time into `bootmode_embedded.h`):

| Mode | Example | Description |
|------|---------|-------------|
| `story scene N` | `story scene 38` | Play scene via story loop (has printf crash risk) |
| `story direct N` | `story direct 25` | Play scene directly, bypass story loop |
| `island ads X N` | `island ads BUILDING.ADS 1` | Play ADS tag directly with island background |
| `story single N` | `story single 10` | Legacy: play one scene via story loop |

**Recommended for testing:** `island ads X.ADS N` — avoids the story init path
which contains printf calls that crash the PS1 game loop.

## Binary Library (Historical Regression Testing)

Build a PS1 executable + CD image for every code-changing commit since the
PS1 port began. This enables testing any scene against any historical build
to find the exact commit that introduced a regression.

```bash
# Build the full library (~15 min, ~6.5GB)
./scripts/build-binary-library.sh

# Preview what will be built
./scripts/build-binary-library.sh --dry-run

# Resume an interrupted build
./scripts/build-binary-library.sh --resume

# Test a historical build against a scene
./scripts/run-regtest.sh --cue binary-library/042_20251120_123456_abc123de/jcreborn.cue \
    --frames 5400 --dumpinterval 120 --dumpdir /tmp/test-042
```

### Output Structure

```
binary-library/
  SUMMARY.txt              # Human-readable build summary
  index.csv                # Spreadsheet: sequence, hash, date, status, sizes
  index.json               # Full metadata for programmatic access
  001_20251018_080849_a8c0599b/
    jcreborn.exe           # PS1 executable
    jcreborn.bin/.cue      # CD image (ready for regtest)
    build.log              # Full build output
    metadata.json          # Commit info + build status + file hashes
  ...
```

### Regression Bisection Workflow

1. Find a scene that's broken: `./scripts/regtest-scene.sh --scene "FISHING 1"`
2. Pick a known-good build from `index.csv` (check the date/message)
3. Test it: `./scripts/run-regtest.sh --cue binary-library/NNN_.../jcreborn.cue`
4. Binary search between good and bad to find the breaking commit
5. Read the breaking commit's diff to understand the root cause

## Known Issues

### printf Crashes in Game Loop

PS1 `printf()` uses the BIOS break instruction for TTY output. This crashes
the game when called during the game loop (after init). Affected paths:

- `fprintf(stderr, ...)` — redirected to printf via macro in `jc_reborn.c`
- `grCaptureEmitFrameMetadataLine()` — capture metadata JSON emission
- `fatalError()` — error reporting (intentional halt)

**Rule:** Never add printf/fprintf to code paths that run during the PS1
game loop. Use visual indicators (LoadImage to VRAM) for PS1 debug output.

### Cold-Boot ADS Scenes (FISHING 1, 2, 6)

Some ADS scripts have all `ADD_SCENE` commands behind `IF_LASTPLAYED`
conditionals. On cold boot (no prior scene context), these conditions are
never satisfied, producing an empty scene. The recovery code tries all
bookmarked chunks but these tags have no unconditional fallback path.

## Docker Images

| Image | Purpose | Build Command |
|-------|---------|---------------|
| `jc-reborn-ps1-dev:amd64` | PS1 compilation (PSn00bSDK) | `./scripts/build-docker-image.sh` |
| `jc-reborn-regtest:latest` | Headless DuckStation regtest | See `config/ps1/Dockerfile.regtest` |

## Scene Status (as of 2026-04-04)

| Family | Verified | Blocked | Total |
|--------|----------|---------|-------|
| ACTIVITY | 10 | 0 | 10 |
| BUILDING | 7 | 0 | 7 |
| FISHING | 5 | 3 | 8 |
| JOHNNY | 6 | 0 | 6 |
| MARY | 5 | 0 | 5 |
| MISCGAG | 2 | 0 | 2 |
| STAND | 14 | 0 | 14 |
| SUZY | 2 | 0 | 2 |
| VISITOR | 6 | 0 | 6 |
| WALKSTUF | 3 | 0 | 3 |
| **Total** | **60** | **3** | **63** |
