# PS1 Left Debug Panel

This file is the permanent reference for the left-side PS1 telemetry overlay.
When a new row is added, removed, or repurposed in code, update this file in the same change.

## Toggle

- Runtime toggle is controlled by `grSetPs1Telemetry(int enabled)` in `graphics_ps1.c`.
- Current default is enabled (`grPs1TelemetryEnabled = 1`).

## Panel 1: Drop/Load Diagnostics (top-left, `y=2`)

Black background with horizontal bars. Width is generally clamped to 140 pixels unless noted.
Current live rows are:

- Row `y=4` (blue `0x001F`): `gStatThreadDrops`
- Row `y=10` (magenta `0x7C1F`): `gStatBmpFrameCapHits`
- Row `y=13` (yellow `0x7FE0`): `gStatBmpShortLoads`
- Marker `y=2` (dim white `0x4210`): `gStatBmpMaxRequested`
- Marker `y=15` (dim white `0x4210`): `gStatBmpMinLoaded`
- Heartbeat marker (white `0x7FFF`): overlay alive indicator
- Moving marker (red `0x001F`): frame-advance heartbeat

## Panel 2: Memory/Resource Pressure (mid-left, `y=174`)

Black background with six rows.

- Row `y=175`:
  - Memory usage percent (`getTotalMemoryUsed() / getMemoryBudget()`)
  - Color by pressure:
    - green `0x03E0`: < 70%
    - cyan `0x03FF`: 70%..84%
    - red `0x001F`: >= 85%
- Row `y=178` (cyan `0x03FF`): loaded BMP count (`gStatLoadedBmp`, capped to 63)
- Row `y=181` (magenta `0x7C1F`): loaded TTM count (`gStatLoadedTtm`, capped to 63)
- Row `y=184` (red `0x001F`): loaded ADS count (`gStatLoadedAds`, capped to 63)
- Row `y=187` (white `0x7FFF`): memory used in 16 KiB units, capped to 63
- Row `y=190` (dim white `0x4210`): memory budget in 16 KiB units, capped to 63

Notes:

- Loaded resource counters refresh every 16 frames in `grRefreshLoadedResourceCounters()`.
- This panel is intended to correlate sprite dropouts/flicker with memory pressure and loaded resource count.

## Panel 3: ADS Runtime Diagnostics (mid-left, `y=90`)

Black background with 13 live rows.

- Row `y=91` (cyan `0x03FF`): active threads (`ps1AdsDbgActiveThreads`)
- Row `y=94` (white `0x7FFF`): mini timer (`ps1AdsDbgMini`)
- Row `y=97` (cyan `0x03FF`): scene signature `((sceneSlot & 0x7) << 3) | (sceneTag & 0x7)`
- Row `y=100` (magenta `0x7C1F`): replay count (`ps1AdsDbgReplayCount`)
- Row `y=103` (cyan `0x03FF`): running thread count (`ps1AdsDbgRunningThreads`)
- Row `y=106` (green `0x03E0`): frame wait / update delay (`grUpdateDelay`)
- Row `y=109` (white `0x7FFF`): replay tries this frame (`ps1AdsDbgReplayTryFrame`)
- Row `y=112` (green `0x03E0`): replay draws this frame (`ps1AdsDbgReplayDrawFrame`)
- Row `y=115` (magenta `0x7C1F`): merged carry-forward draws (`ps1AdsDbgMergeCarryFrame`)
- Row `y=118` (red `0x001F`): played threads with zero draws (`ps1AdsDbgNoDrawThreadsFrame`)
- Row `y=121` (cyan `0x03FF`): threads played this frame (`ps1AdsDbgPlayedThreadsFrame`)
- Row `y=124` (yellow `0x7FE0`): total recorded sprites this frame (`ps1AdsDbgRecordedSpritesFrame`)
- Row `y=127` (red `0x001F`): terminated thread count (`ps1AdsDbgTerminatedThreads`)

## Panel 4: Story Transition Diagnostics (bottom-left, `y=222`)

Black background with five rows.

- Row `y=223` (dim white `0x4210`): `ps1StoryDbgSeq`
- Row `y=226` (white `0x7FFF`): `ps1StoryDbgPhase`
- Row `y=229` (green `0x03E0`): `ps1StoryDbgSceneTag`
- Row `y=232` (cyan `0x03FF`): `prev` signature `((spot & 0x7) * 8) + (hdg & 0x7)`
- Row `y=235` (yellow `0x7FE0`): `next` signature `((spot & 0x7) * 8) + (hdg & 0x7)`

## Panel 5: Pilot Pack Diagnostics (upper-left, `y=30`)

Black background with three rows for the current scene-pack runtime path.

- Row `y=31` (white `0x7FFF`): active pilot pack id (`ps1PilotDbgActivePack`)
- Row `y=34` (green `0x03E0`): cumulative successful pack loads (`ps1PilotDbgHits`)
- Row `y=37` (red `0x001F`): cumulative fallback loads after pack-first lookup (`ps1PilotDbgFallbacks`)

Notes:

- `ps1PilotDbgActivePack` is a compact runtime pack id, not an ADS enum.
- Non-zero `ps1PilotDbgHits` proves the compiled pack payload path was used at runtime.
- Non-zero `ps1PilotDbgFallbacks` means the runtime stayed functional by falling back to the extracted-file path for at least one resource.

## Maintenance Rule

- Keep these panels permanent in PS1 builds.
- Any change to bar meaning, color, scale, or position must update this file and the relevant inline comments in `graphics_ps1.c`.

## Tooling

Use `scripts/decode-ps1-bars.py` to decode bar widths from screenshots instead of manual counting.
The decoder currently targets only the live panels listed above.

Examples:

- `scripts/decode-ps1-bars.py "/path/to/screenshot.png"`
- `scripts/decode-ps1-bars.py --json "/path/to/screenshot.png"`
- `scripts/decode-ps1-bars.py --include-zero "/path/to/screenshot.png"`

Defaults are tuned for the current overlay geometry (`x0=4`, `width=90`).
If panel geometry changes, update this document and script defaults together.
