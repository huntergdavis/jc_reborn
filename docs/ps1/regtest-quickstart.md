# PS1 Headless Regression Testing Harness

## What It Is

A fully headless PS1 emulation testing system built on DuckStation's `duckstation-regtest` binary, running in Docker. No display server needed. Runs at **520 FPS** (8x realtime) — 30 seconds of PS1 gameplay in ~3.6 seconds of wall time.

## Docker Image

Pre-built image: `jc-reborn-regtest:latest` (363MB). Built from `config/ps1/Dockerfile.regtest` — multi-stage build that compiles DuckStation from source (Ubuntu 24.04, clang-18) with `BUILD_REGTEST=ON`, then extracts just the headless binary + prebuilt shared libs into a slim runtime image.

To rebuild from scratch (one-time, ~15-20 min):
```bash
./scripts/build-regtest-image.sh
```

## Running a Test

Minimal example — run a late-window capture and keep only frames at or after the reviewed scene start:
```bash
docker run --rm \
  -v "$PWD":/game:ro \
  -v ~/.var/app/org.duckstation.DuckStation/config/duckstation/bios:/root/.local/share/duckstation/bios:ro \
  -v /tmp/regtest-out:/output \
  --entrypoint duckstation-regtest \
  jc-reborn-regtest:latest \
  -renderer Software -console -frames 3600 -dumpdir /output -dumpinterval 60 \
  -- /game/jcreborn.cue
```

Or use the wrapper script:
```bash
./scripts/run-regtest.sh --frames 3600 --start-frame 2400 --dumpinterval 60 --dumpdir /tmp/regtest-out
```

## What It Captures

1. **Frame PNGs** — saved to `<dumpdir>/jcreborn/frame_NNNNN.png` at configurable intervals
2. **PS1 printf/TTY output** — DuckStation intercepts BIOS putchar; all PS1 `printf()` goes to stdout
3. **State hashes** — SHA256 of RAM, VRAM, SPU RAM, and full save state at exit (for deterministic regression detection)
4. **Telemetry bars** — the left-side diagnostic overlay renders in captured frames, decodable by `scripts/decode-ps1-bars.py`

## Key CLI Flags (duckstation-regtest)

| Flag | Description |
|------|-------------|
| `-frames N` | Run exactly N frames then exit |
| `-dumpdir PATH` | Save frame PNGs to this directory |
| `-dumpinterval N` | Capture every Nth frame |
| `-renderer Software` | Use software renderer (deterministic) |
| `-console` | Enable console/TTY logging to stdout |
| `-log <level>` | Log verbosity: Error, Warning, Info, Verbose, Debug |

## PS1 Test Instrumentation

When the PS1 binary is built with `-DPS1_TEST_BUILD=ON`, structured test output is emitted via printf (captured by regtest on stdout):

```
[TEST:SCENE] play STAND.ADS tag=2
[TEST:FRAME] display frame=0 delay=3
[TEST:PERF] upload tiles=2 rows=87
[TEST:PERF] heartbeat frame=60
[TEST:STATE] load_bmp slot=0 sprites=42
[TEST:ERROR] fatal: out of memory
```

Categories: `SCENE` (lifecycle), `FRAME` (per-frame), `PERF` (metrics), `ERROR` (crashes), `STATE` (resources), `ASSERT` (invariants). All compiled out in normal builds via `ps1_test.h` macros.

Build a test-instrumented binary:
```bash
docker run --rm --platform linux/amd64 \
  -v "$PWD":/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project/build-ps1 && cmake -DCMAKE_BUILD_TYPE=Release -DPS1_TEST_BUILD=ON .. && make -j4 jcreborn"
```

## Orchestration Scripts

| Script | Purpose |
|--------|---------|
| `scripts/run-regtest.sh` | Run single test via Docker with full option set |
| `scripts/regtest-scene.sh` | Test one scene: rebuilds CD with boot override, uses reviewed scene windows, decodes telemetry, outputs JSON |
| `scripts/regtest-all-scenes.sh` | Parallel runner for multiple scenes (`--parallel 2`, `--verified-only`) |
| `scripts/analyze-regtest.py` | Post-run analysis: frame inspection, telemetry decode, HTML report, regression detection |
| `scripts/regtest-compare.sh` | Diff two test runs for before/after comparison |

## Scene Configuration

`config/ps1/regtest-scenes.txt` lists all 63 scenes with status:
```
STAND 1 verified
STAND 2 verified
...
ACTIVITY 4 bringup
BUILDING 1 blocked
FISHING 1 blocked
MARY 1 untested
```

## BIOS Requirement

DuckStation needs a PS1 BIOS file. Mount it to `/root/.local/share/duckstation/bios/` inside the container. The file is at `~/.var/app/org.duckstation.DuckStation/config/duckstation/bios/scph1001.bin` on the host.

## Key Files

```
config/ps1/Dockerfile.regtest          -- Docker image build
config/ps1/regtest-config.sh           -- Shared test configuration
config/ps1/regtest-scenes.txt          -- All 63 scenes with status
scripts/run-regtest.sh                 -- Docker wrapper
scripts/regtest-scene.sh               -- Single-scene test runner
scripts/regtest-all-scenes.sh          -- Parallel orchestrator
scripts/analyze-regtest.py             -- Results analyzer + HTML report
scripts/regtest-compare.sh             -- Diff two runs
scripts/decode-ps1-bars.py             -- Telemetry bar decoder (existing)
ps1_test.h                             -- Test instrumentation macros
docs/ps1/regtest-harness.md            -- Full architecture documentation
```
