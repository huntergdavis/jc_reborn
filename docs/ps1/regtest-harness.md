# PS1 Regression Test Harness

Automated PS1 regression testing for scene rollout, using:

- headless `duckstation-regtest` for scalable batch/frame capture
- the interactive DuckStation harness for exact-story validation on the desktop

## Overview

DuckStation includes a purpose-built regression test runner (`duckstation-regtest`) that:

- Runs PS1 games **without any display** (no GPU, no window manager needed)
- Uses the **Software renderer** for deterministic, reproducible output
- **Captures frames as PNGs** at configurable intervals
- **Forwards PS1 printf/TTY output to stdout** so game debug prints appear in logs
- Runs a fixed number of frames then exits, producing SHA256 hashes of system state (RAM, VRAM, SPU RAM, save state)
- Supports **fast boot** (skips BIOS animation)

This is ideal for CI pipelines, automated testing, and catching visual regressions between builds.

## Architecture

```
 Host machine
 +---------------------------------------------------+
 |  run-regtest.sh                                    |
 |    |                                               |
 |    +-> docker run jc-reborn-regtest:latest          |
 |         |                                          |
 |         +-> duckstation-regtest                     |
 |              -renderer Software                    |
 |              -frames 1800                          |
 |              -dumpdir /output/frames               |
 |              -dumpinterval 60                      |
 |              -console                              |
 |              -- /game/jcreborn.cue                 |
 |                                                    |
 |    Bind mounts:                                    |
 |      CD image dir  -> /game     (read-only)        |
 |      BIOS dir      -> /bios     (read-only)        |
 |      Output dir    -> /output   (read-write)       |
 +---------------------------------------------------+

 Output:
   regtest-results/YYYYMMDD-HHMMSS/
     regtest.log          Full stdout+stderr
     tty-output.txt       PS1 printf lines only
     frames/
       frame_00000.png    Captured at interval
       frame_00060.png
       frame_00120.png
       ...
```

## Setup

### Prerequisites

- Docker (rootless or with your user in the `docker` group)
- A PS1 BIOS image (e.g., `scph1001.bin`)
- A built CD image (`jcreborn.cue` + `.bin`)

### 1. Build the Docker Image (One-Time)

```bash
./scripts/build-regtest-image.sh
```

This compiles DuckStation from source inside Docker. It takes 15-30 minutes on first run but is fully cached by Docker layers afterward.

To force a clean rebuild:

```bash
./scripts/build-regtest-image.sh --no-cache
```

The resulting image is tagged `jc-reborn-regtest:latest`.

### 2. Prepare a CD Image

If you have not already built one:

```bash
./scripts/build-ps1.sh
./scripts/make-cd-image.sh
```

### 3. BIOS Location

The regtest script auto-detects BIOS files in common locations:

- `~/.local/share/duckstation/bios/`
- `~/.config/duckstation/bios/`
- `~/.var/app/org.duckstation.DuckStation/config/duckstation/bios/`
- `~/ps1-bios/`
- `./bios/`

Or specify explicitly: `--bios /path/to/bios/`

## Scene Routing Model

The current rollout no longer treats raw `island ads <ADS> <tag>` boots as the
primary truth path.

Instead, scene verification should prefer exact story-scene entry:

```bash
story scene <index>
```

That preserves the same story metadata and handoff context used by the real
game. This matters for families such as `ACTIVITY`, `MARY`, `VISITOR`, and
`FISHING`, where a raw ADS launch can reach bootstrap/ocean states that are not
valid certification routes.

The generated scene list at
[config/ps1/regtest-scenes.txt](/home/hunter/workspace/jc_reborn/config/ps1/regtest-scenes.txt)
is derived from the current rollout manifest and records:

```text
ADS_NAME TAG SCENE_INDEX STATUS BOOTMODE...
```

So the verification surface stays aligned with the real rollout status rather
than a hand-maintained list.

## Running Tests

### Single Test Run

```bash
# Defaults: 1800 frames (30s), capture every 60 frames (1/sec)
./scripts/run-regtest.sh

# Custom settings
./scripts/run-regtest.sh \
  --cue build-ps1/jcreborn.cue \
  --frames 3600 \
  --dumpinterval 30 \
  --bios ~/ps1-bios/
```

### Command-Line Options

| Option              | Default               | Description                                   |
|---------------------|-----------------------|-----------------------------------------------|
| `--frames N`        | 1800                  | Total frames to execute (60fps)               |
| `--dumpinterval N`  | 60                    | Capture a frame every N frames                |
| `--dumpdir DIR`     | `regtest-results/`    | Output root directory                         |
| `--cue FILE`        | auto-detect           | Path to `.cue` file                           |
| `--bios DIR`        | auto-detect           | Directory containing PS1 BIOS files           |
| `--renderer NAME`   | Software              | Software, Vulkan, OpenGL                      |
| `--log LEVEL`       | Info                  | Error, Warning, Info, Verbose, Debug          |
| `--timeout SECS`    | 120                   | Wall-clock timeout (kills runaway tests)      |
| `--upscale N`       | (native)              | Resolution multiplier                         |
| `--cpu MODE`        | (default)             | Interpreter, CachedInterpreter, Recompiler    |

### Environment Variables

Defaults come from `config/ps1/regtest-config.sh`:

```bash
REGTEST_FRAMES=1800
REGTEST_INTERVAL=60
REGTEST_OUTPUT_DIR=regtest-results
REGTEST_TIMEOUT=120
REGTEST_PARALLEL=4
```

## Interpreting Results

### Frame PNGs

Captured frames are saved as `frame_NNNNN.png` where `NNNNN` is the frame number. With default settings (1800 frames, interval 60), you get 30 PNGs showing one frame per second of gameplay.

### PS1 Printf/TTY Output

Any `printf()` calls in the PS1 game code appear in `tty-output.txt`. This is invaluable for debugging since the PS1 has no debug console — the emulator captures the guest's TTY writes and forwards them to stdout.

The full log (`regtest.log`) contains both DuckStation host messages and PS1 guest output.

### System State Hashes

At completion, DuckStation logs SHA256 hashes of:
- **Save state** — complete machine snapshot
- **RAM** — main 2MB
- **SPU RAM** — sound memory
- **VRAM** — video memory

These hashes enable deterministic regression detection: if the same game, BIOS, and frame count produce different hashes, something changed.

### Exit Codes

| Code | Meaning                          |
|------|----------------------------------|
| 0    | Success — completed all frames   |
| 124  | Timeout — killed by wall-clock   |
| Other| DuckStation error                |

## Regression Detection via Frame Hashing

To detect visual regressions between builds:

1. **Establish a baseline**: run regtest on a known-good build, save the frame PNGs.
2. **Run on new build**: same settings, same BIOS, same frame count.
3. **Compare**: MD5-hash each frame PNG and diff against baseline.

DuckStation includes `scripts/check_regression_tests.py` for this. Adapted for our workflow:

```bash
# After running two tests:
# baseline:  regtest-results/baseline/frames/
# candidate: regtest-results/candidate/frames/

# Compare with MD5 hashes (simple approach)
md5sum regtest-results/baseline/frames/*.png > /tmp/baseline.md5
cd regtest-results/candidate/frames/
md5sum -c /tmp/baseline.md5

# Or use DuckStation's HTML diff report generator
python3 path/to/check_regression_tests.py \
  regtest-results/baseline \
  regtest-results/candidate \
  -o regtest-results/diff-report.html
```

Any mismatched frames indicate a visual regression. The HTML report shows side-by-side comparisons with an interactive viewer.

## Parallel Testing

For testing multiple scenes, run concurrent Docker containers:

```bash
# Test several scenes in parallel
for scene in STAND JOHNNY WALKSTUF; do
    ./scripts/run-regtest.sh \
      --dumpdir "regtest-results/${scene}" \
      --frames 1800 &
done
wait
```

The `REGTEST_PARALLEL` config variable (default 4) is intended for future batch-test scripts that manage concurrency limits.

Each Docker container is isolated, so parallel runs do not interfere with each other.

## Troubleshooting

### "Docker image not found"
Run `./scripts/build-regtest-image.sh` first.

### "No .cue file found"
Build the CD image: `./scripts/build-ps1.sh && ./scripts/make-cd-image.sh`

### Test hangs / timeout
The `--timeout` flag (default 120s) kills hung tests. If the game loops forever, it will be terminated. Check `regtest.log` for the last activity.

### No frames captured
Verify the frame count is greater than the dump interval. With `--frames 60 --dumpinterval 60`, only one frame is captured.

### BIOS errors
DuckStation requires a real PS1 BIOS for full compatibility. With `-fastboot`, it can sometimes work without one, but results may differ. Place your BIOS file in one of the auto-detected directories or use `--bios`.

### Software renderer only
The Docker container has no GPU access by default. Use `--renderer Software` (the default). For GPU-accelerated testing, you would need to pass through GPU devices, which is beyond the scope of this harness.
### Scene-Scoped Desktop Validation

For exact-story validation on the desktop harness:

```bash
./scripts/auto-test-ps1.sh 50 story scene 38
./scripts/auto-test-ps1.sh 50 story scene 33
```

For rebuild-and-watch runs:

```bash
./scripts/rebuild-and-let-run.sh noclean "story scene 38"
```

The desktop harness now prefers DuckStation-native screenshots and refuses
whole-desktop fallback capture unless `PS1_ALLOW_FALLBACK_CAPTURE=1` is set.
That avoids false verification from terminal or unrelated-window captures.

### Overlay-Backed PS1 Character Checks

For PS1 bug fixing, the preferred screenshot harness is now:

1. launch a controlled DuckStation run with `capture-overlay`
2. take a DuckStation screenshot
3. decode the embedded overlay into character truth
4. compare against expected truth
5. open the generated HTML diff report

Examples:

```bash
./scripts/auto-test-ps1.sh 35 --overlay "story scene 17"
./scripts/capture-duckstation-scene.sh --scene "FISHING 1" --overlay
```

Single-screenshot check:

```bash
python3 scripts/check-character-screenshot.py \
  --image ~/.var/app/org.duckstation.DuckStation/config/duckstation/screenshots/<shot>.png \
  --expected-root host-script-review/fishing1 \
  --out-dir /tmp/ps1-character-check
```

Notes:
- `check-character-screenshot.py` now prefers the frame number embedded in the overlay packet, so DuckStation timestamped filenames do not need manual frame numbering.
- If `--expected-root` contains only one scene, `--scene-label` is optional and will be inferred automatically.
- This path is intended for controlled test captures we generate ourselves. Arbitrary screenshots without the overlay still need a separate image-matching path.
