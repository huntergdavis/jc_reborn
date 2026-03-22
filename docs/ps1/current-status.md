# PS1 Port - Current Status

Current progress and metrics for the PlayStation 1 port. Last updated 2026-03-21.

## Overall Status: Boots and Runs on DuckStation

The game boots, loads resources from CD, runs scene animations, and cycles through the story loop. 25 of 63 scenes are verified correct; the remaining scenes have authored restore specs but need validation.

| Component | Status | Progress |
|-----------|--------|----------|
| Build System | Complete | 100% |
| CD-ROM I/O | Complete | 100% |
| Graphics Layer | Complete | ~100% |
| Input Layer | Complete | 100% |
| Resource System | Complete | 100% |
| Scene Restore Pipeline | Active development | ~40% verified |
| Audio Layer | Skeleton only | 40% |
| Telemetry / Debug | Complete | 100% |

## Verified Scenes: 25 / 63

Scenes are verified by forced-scene boot with telemetry capture on DuckStation.

| ADS File | Tags Verified | Count |
|----------|---------------|-------|
| STAND.ADS | 1-12, 15-16 | 14 |
| JOHNNY.ADS | 1-6 | 6 |
| WALKSTUF.ADS | 1-3 | 3 |
| MISCGAG.ADS | 1-2 | 2 |
| **Total** | | **25** |

### Bring-Up (in header, not yet verified)

- ACTIVITY.ADS tag 4 -- live in restore pilots header but has a stale extra-Johnny climb frame; needs fix before promotion to verified.

### Blocked

- BUILDING.ADS -- needs trustworthy entry/validation paths
- FISHING.ADS -- needs trustworthy entry/validation paths

## Build System

- Docker container `jc-reborn-ps1-dev:amd64` with PSn00bSDK
- CMake cross-compilation targeting MIPS (PSn00bSDK toolchain)
- `mkpsxiso` for CD image generation
- Build script: `./scripts/rebuild-and-let-run.sh noclean` (or `./scripts/build-ps1.sh`)
- Builds and runs routinely; no manual intervention needed

## Graphics Layer (graphics_ps1.c -- 3300+ lines)

Complete software compositing pipeline. Key subsystems:

- GPU init with double buffering (640x480 interlaced)
- Dirty-rect system: ~80-95% reduction in per-frame data movement
- 4-bit indexed sprite format (indexedPixels) with palette LUT -- ~4x RAM savings over 15-bit
- 4-pixel unrolled compositing with opaque sprite fast-path
- Palette LUT acceleration: ~15-25% compositing speedup
- Layer composition with 5-layer depth sorting
- VRAM management with wraparound tracking
- BMP sprite loading supporting multi-image Sierra BMP format (up to 120 sprites per BMP)

## CD-ROM I/O (cdrom_ps1.c -- 2280 lines)

Fully implemented. Reads resource files from CD image using PSn00bSDK CD-ROM APIs (CdSearchFile, CdRead, CdSync). All game data loads from disc at runtime.

## Resource System

- Hash-based O(1) resource lookups (replaced O(N) strcmp scanning)
- LRU cache with pinning for active resources
- All ADS files decompressed at startup (~16KB total)
- findTtmResource/findAdsResource/findScrResource return NULL on PS1 instead of fatalError

## Input Layer (events_ps1.c -- 136 lines)

100% complete.

| PSX Button | Action |
|-----------|--------|
| Start | Pause/Unpause |
| Select | Toggle debug |
| Triangle | Advance frame (paused) |
| Circle | Toggle max speed |

## Audio Layer (sound_ps1.c -- 184 lines)

40% -- skeleton only. SPU initialization and shutdown are implemented. No actual audio playback yet.

**TODO:**
- [ ] WAV to PS1 ADPCM (VAG) conversion
- [ ] SPU RAM uploading (soundLoad)
- [ ] Voice channel allocation (soundPlay)
- [ ] ADSR envelope setup

## Scene Restore System

The core architectural innovation for the PS1 port. Replaces replay-based scene continuity with offline-authored scene contracts.

### Offline Pipeline

```
scene analyzer -> restore specs -> cluster contracts -> pack compiler -> header generator
```

- 63 scene-scoped restore specs generated (one per scene across all ADS files)
- Compressed into 34 cluster contracts (scenes sharing identical resource sets)
- `ps1_restore_pilots.h`: auto-generated, 1000 lines, 26 pilot entries
- Pack-authoritative scene loading replacing generic runtime discovery
- Replay continuity being removed family-by-family as correctness is proven

### Scripts

- `build-restore-pilot-spec.py` -- per-scene restore spec generation
- `cluster-restore-scene-specs.py` -- cluster contract compression
- `compile-scene-pack.py` -- PSB scene pack compilation
- `generate-restore-pilots-header.py` -- C header generation
- `rank-restore-candidates.py` -- candidate ranking for rollout
- `plan-restore-rollout.py` -- rollout planning

## Telemetry / Debug

5-panel diagnostic overlay for DuckStation debugging:
- Scene identification and tag tracking
- Resource cache state
- Frame timing
- Memory usage
- Restore pilot state

Visual debugging via colored pixels (LoadImage) since printf() crashes in the game loop on PS1.

## Performance (2026-03 optimization sprint)

| Optimization | Impact |
|-------------|--------|
| Dirty-rect system | ~80-95% reduction in per-frame data movement |
| Palette LUT + 4-pixel unroll | ~15-25% compositing speedup |
| 4-bit indexed sprites | ~4x RAM savings vs 15-bit direct |
| Hash-based resource lookup | O(1) vs O(N) strcmp |
| Opaque sprite fast-path | Skips transparency checks |

### Binary Size

- PS-EXE: 120KB
- Text segment: ~107KB
- Data segment: ~10KB

## Architecture

8-phase implementation plan, currently in Phase 7 (dirty-region restore model).

**Phase progression:**
1. Build infrastructure
2. Core platform layers (graphics, input, audio skeleton)
3. CD-ROM I/O and resource loading
4. First boot and rendering
5. Scene playback and story loop
6. Performance optimization (hash tables, indexed sprites, dirty-rect)
7. Dirty-region restore model (current)
8. Full scene coverage and polish

## Next Steps

### Immediate
1. Fix ACTIVITY.ADS tag 4 stale frame -- promote to verified (25 -> 26)
2. Fix blocked entry paths for BUILDING.ADS and FISHING.ADS
3. Expand verified scenes from 25 to 63 via cluster contract promotion

### Short-term
4. Build robust automated validation harness (batch scene verification)
5. Complete audio implementation (WAV to VAG, SPU playback)
6. PSB/BMP hot path reconciliation

### Long-term
7. Real hardware testing
8. Title screen and loading screens
9. Screen effects (fade, rain)
10. Memory card save/load

## Known Issues

1. **ACTIVITY.ADS tag 4**: Stale extra-Johnny climb frame in restore pilot
2. **BUILDING.ADS / FISHING.ADS**: Blocked on entry path validation
3. **Audio**: No playback (skeleton only)
4. **printf() in game loop**: Causes crashes/hangs on PS1 -- must use visual debugging

## File Inventory

### PS1 Implementation
```
graphics_ps1.c         3359 lines  (complete)
cdrom_ps1.c            2280 lines  (complete)
sound_ps1.c             184 lines  (40% - skeleton)
events_ps1.c            136 lines  (complete)
ps1_restore_pilots.h   1000 lines  (auto-generated, 26 pilots)
```

### Offline Pipeline Scripts
```
build-restore-pilot-spec.py
cluster-restore-scene-specs.py
compile-scene-pack.py
generate-restore-pilots-header.py
rank-restore-candidates.py
plan-restore-rollout.py
extract-dirty-region-templates.py
decode-ps1-bars.py
```

## See Also

- [Project History](project-history.md) - Development journey and lessons learned
- [Development Workflow](development-workflow.md) - Build and test procedures
- [Hardware Specs](hardware-specs.md) - PS1 technical details
- [Graphics Layer](graphics-layer.md) - GPU implementation details
- [Research Package](research/README.md) - Scene-pack and restore research
