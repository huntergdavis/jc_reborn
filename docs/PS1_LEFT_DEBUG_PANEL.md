# PS1 Left Debug Panel

This file is the permanent reference for the left-side PS1 telemetry overlay.
When a new row is added, removed, or repurposed in code, update this file in the same change.

## Toggle

- Runtime toggle is controlled by `grSetPs1Telemetry(int enabled)` in `graphics_ps1.c`.
- Current default is enabled (`grPs1TelemetryEnabled = 1`).

## Panel 1: Drop/Load Diagnostics (top-left, `y=2`)

Black background with horizontal bars. Width is generally clamped to 140 pixels unless noted.

- Row `y=4` (blue `0x001F`): `gStatThreadDrops`
- Row `y=7` (cyan `0x03FF`): `gStatReplayDrops`
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

Black background with 28 rows.

- Row `y=91` (cyan `0x03FF`): `ps1AdsDbgActiveThreads`
- Row `y=94` (white `0x7FFF`): `ps1AdsDbgMini`
- Row `y=97` (red `0x001F`): `ps1AdsDbgStalledFrames >> 2`
- Row `y=100` (green `0x03E0`): `ps1AdsDbgProgressPulse`
- Row `y=103` (cyan `0x03FF`): scene signature `((slot & 0x7) << 3) | (tag & 0x7)`
- Row `y=106` (magenta `0x7C1F`): `ps1AdsDbgReplayCount`
- Row `y=109` (cyan `0x03FF`): `ps1AdsDbgRunningThreads`
- Row `y=112` (red `0x001F`): `ps1AdsDbgTerminatedThreads`
- Row `y=115` (white `0x7FFF`): `ps1AdsDbgThreadTimer`
- Row `y=118` (dim white `0x4210`): `ps1AdsDbgThreadDelay`
- Row `y=121` (green `0x03E0`): `grUpdateDelay`
- Row `y=124` (white `0x7FFF`): replay attempts this frame (`ps1AdsDbgReplayTryFrame`)
- Row `y=127` (green `0x03E0`): replay draws applied this frame (`ps1AdsDbgReplayDrawFrame`)
- Row `y=130` (red `0x001F`): replay rejects by scene epoch mismatch (`ps1AdsDbgReplayRejectEpoch`)
- Row `y=133` (magenta `0x7C1F`): replay rejects by slot generation mismatch (`ps1AdsDbgReplayRejectGen`)
- Row `y=136` (cyan `0x03FF`): replay rejects by slot/image invalid (`ps1AdsDbgReplayRejectSlot`)
- Row `y=139` (yellow `0x7FE0`): replay rejects by missing sprite pointer/pixels (`ps1AdsDbgReplayRejectSprite`)
- Row `y=142` (blue `0x001F`): replay flipped draws this frame (`ps1AdsDbgReplayFlipFrame`)
- Row `y=145` (dim white `0x4210`): scene sequence (`ps1AdsDbgSceneSeq`)
- Row `y=148` (white `0x7FFF`): scene frame count (`ps1AdsDbgSceneFrames`)
- Row `y=151` (cyan `0x03FF`): scene replay attempts (`ps1AdsDbgSceneTry`)
- Row `y=154` (green `0x03E0`): scene replay draws (`ps1AdsDbgSceneDraw`)
- Row `y=157` (red `0x001F`): scene epoch rejects (`ps1AdsDbgSceneRejectEpoch`)
- Row `y=160` (magenta `0x7C1F`): scene slotGen rejects (`ps1AdsDbgSceneRejectGen`)
- Row `y=163` (cyan `0x03FF`): scene slot/image rejects (`ps1AdsDbgSceneRejectSlot`)
- Row `y=166` (yellow `0x7FE0`): scene sprite-null rejects (`ps1AdsDbgSceneRejectSprite`)
- Row `y=169` (white `0x7FFF`): scene max stall frames (`ps1AdsDbgSceneStallMax >> 2`)
- Row `y=172` (red `0x001F`): no-draw streak (`ps1AdsDbgNoDrawStreak >> 1`)

Scene cumulative rows reset when the active scene signature (`sceneSlot`, `sceneTag`) changes.

## Panel 4: Story Transition Diagnostics (bottom-left, `y=222`)

Black background with five rows.

- Row `y=223` (dim white `0x4210`): `ps1StoryDbgSeq`
- Row `y=226` (white `0x7FFF`): `ps1StoryDbgPhase`
- Row `y=229` (green `0x03E0`): `ps1StoryDbgSceneTag`
- Row `y=232` (cyan `0x03FF`): `prev` signature `((spot & 0x7) * 8) + (hdg & 0x7)`
- Row `y=235` (yellow `0x7FE0`): `next` signature `((spot & 0x7) * 8) + (hdg & 0x7)`

## Maintenance Rule

- Keep these panels permanent in PS1 builds.
- Any change to bar meaning, color, scale, or position must update this file and the relevant inline comments in `graphics_ps1.c`.

## Tooling

Use `scripts/decode-ps1-bars.py` to decode bar widths from screenshots instead of manual counting.

Examples:

- `scripts/decode-ps1-bars.py "/path/to/screenshot.png"`
- `scripts/decode-ps1-bars.py --json "/path/to/screenshot.png"`
- `scripts/decode-ps1-bars.py --include-zero "/path/to/screenshot.png"`

Defaults are tuned for the current overlay geometry (`x0=4`, `width=90`).
If panel geometry changes, update this document and script defaults together.
