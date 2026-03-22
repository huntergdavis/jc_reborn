# PS1 Port - Project History

The development journey, challenges, and lessons learned from porting Johnny Reborn to PlayStation 1.

## Project Overview

Started from the optimized `4mb2025` branch (350KB memory usage). The port now boots and runs on DuckStation, cycling through 25 verified scenes with a complete software compositing pipeline, CD-ROM I/O, and an offline scene-restore system.

**Timeline**: Initial session (2025-10-18), ongoing development through 2026-03
**Branch**: `ps1` (based on `4mb2025`)
**Development Tools**: PSn00bSDK, Docker, DuckStation emulator
**Binary Size**: 120KB PS-EXE

## Why PS1?

The PlayStation 1 is a perfect target for Johnny Reborn:

### Technical Fit
- **Native 640x480 resolution**: Exact match for engine design
- **2MB RAM**: 6x more than peak usage (350KB)
- **Hardware sprite engine**: Fast sprite rendering
- **CLUT color system**: Perfect for indexed color sprites
- **24 hardware audio channels**: Simultaneous sound effects

### Nostalgic Appeal
- Both Johnny Castaway (1992) and PS1 (1994) are from the same era
- Bringing a classic screensaver to a classic console
- Demonstrates retro game development techniques

## Development Phases

### Phase 1: Research & Infrastructure

**Goals**: Understand PS1 development ecosystem and set up tooling

**Achievements**:
- ✅ Selected PSn00bSDK over official Sony SDK (modern, open source)
- ✅ Researched PS1 hardware capabilities (perfect match!)
- ✅ Downloaded DuckStation emulator (best modern PS1 emulator)
- ✅ Attempted precompiled macOS toolchain (partial success)
- ✅ Created Docker-based solution (works on all platforms)

**Key Decision**: Docker over native toolchain
- Avoids platform-specific toolchain issues
- Reproducible builds
- Easy CI/CD integration

### Phase 2: Build System

**Goals**: Configure CMake and CD image generation

**Achievements**:
- ✅ CMakeLists.ps1.txt - Complete CMake configuration
- ✅ cd_layout.xml - CD-ROM layout
- ✅ build-ps1.sh - Automated build script
- ✅ Dockerfile.ps1 - Docker environment

**Key Decision**: CMake over Makefiles
- PSn00bSDK provides CMake support
- Better cross-platform compatibility
- Easier dependency management

### Phase 3: Core Implementation

**Goals**: Port platform-specific layers (graphics, sound, input)

**Achievements**:
- graphics_ps1.c/h (initially 700 lines, later grew to 3300+)
- events_ps1.c/h (136 lines, 100% complete)
- sound_ps1.c/h (184 lines, 40% skeleton)

**Key Decision**: Keep core engine unchanged
- Only 3 files needed porting
- 4,000+ lines reused without modification
- 63% code reuse ratio

### Phase 4: CD-ROM I/O and First Boot

**Goals**: Load game data from disc, boot on DuckStation

**Achievements**:
- cdrom_ps1.c (2280 lines) -- complete CD-ROM file I/O replacing stdio
- CdSearchFile / CdRead / CdSync integration with PSn00bSDK
- Resource loading from CD image working end-to-end
- First successful boot on DuckStation

**Key Decision**: Keep fopen/fread abstraction thin
- cdrom_ps1.c reimplements file I/O against CD sectors
- Rest of resource.c unchanged

### Phase 5: Scene Playback and Story Loop

**Goals**: Play back TTM animations, cycle through story scenes

**Achievements**:
- Story loop driving scene selection
- TTM animation playback running
- ADS files decompressed at startup (~16KB total)
- findTtmResource/findAdsResource/findScrResource return NULL on PS1 instead of fatalError

**Key Challenge**: printf() crashes in the game loop on PS1
- Discovered that vprintf (used by debugMsg) also crashes
- Solution: visual debugging via colored pixels (LoadImage)
- debugMode=0 disables text output paths

### Phase 6: Performance Optimization (2026-03)

**Goals**: Make compositing fast enough for smooth playback, reduce memory pressure

**Achievements**:
- 4-bit indexed sprite format (indexedPixels) -- ~4x RAM savings over 15-bit direct color
- Palette LUT for compositing -- avoids per-pixel palette lookups
- 4-pixel unrolled compositing loop
- Opaque sprite fast-path (skips transparency checks)
- Hash-based O(1) resource lookups replacing O(N) strcmp scanning
- Dirty-rect system: ~80-95% reduction in per-frame data movement

**Results**:
- Binary reduced from ~124KB to ~120KB
- Compositing ~15-25% faster
- Per-frame data movement reduced by 80-95%

### Phase 7: Scene Restore System (2026-03, current)

**Goals**: Replace replay-based scene continuity with deterministic offline contracts

**Problem**: The original engine relies on "replay continuity" -- playing prior scenes to establish state for the current scene. On PS1 with CD latency and 2MB RAM, this is impractical.

**Solution**: Offline-authored scene restore contracts.

**Offline Pipeline**:
```
scene analyzer -> restore specs -> cluster contracts -> pack compiler -> header generator
```

**Achievements**:
- 63 scene-scoped restore specs generated (every scene across all ADS files)
- Compressed into 34 cluster contracts (scenes sharing identical resource sets)
- ps1_restore_pilots.h: auto-generated C header, 1000 lines, 26 active pilots
- Pack-authoritative scene loading replacing generic runtime discovery
- Replay continuity being removed family-by-family as correctness is proven
- 25 scenes verified correct on DuckStation
- 5-panel telemetry overlay for debugging restore state
- Forced-scene boot harness for targeted validation

**Key Insight**: Scenes within the same ADS family often share the same BMP/SCR resource set. Clustering reduces 63 individual restore specs down to 34 contracts, minimizing header bloat.

**Scripts developed**:
- build-restore-pilot-spec.py
- cluster-restore-scene-specs.py
- compile-scene-pack.py
- generate-restore-pilots-header.py
- rank-restore-candidates.py
- plan-restore-rollout.py
- extract-dirty-region-templates.py
- decode-ps1-bars.py

## Challenges & Solutions

### Challenge 1: Toolchain Setup
**Problem**: Precompiled macOS toolchain incomplete (missing cc1, cc1plus)  
**Attempted**: Build from source (failed - needs Linux)  
**Solution**: ✅ Docker-based build environment

**Lesson Learned**: Docker solves "works on my machine" problems

### Challenge 2: API Differences
**Problem**: SDL2 vs. PSn00bSDK have different paradigms  
**Solution**: ✅ Abstraction layer with PS1Surface structure

**Lesson Learned**: Clean abstractions enable platform portability

### Challenge 3: VRAM Management
**Problem**: 1MB VRAM limit, sprites must be cached efficiently
**Solution**: Automatic allocation tracking with wraparound, LRU eviction

**Lesson Learned**: Careful resource management critical for embedded systems

### Challenge 4: CD Latency
**Problem**: CD-ROM 2x speed is slow (150ms seeks)
**Solution**: LRU cache with pinning, pre-loading critical data, scene-scoped restore contracts that declare exactly which resources are needed

**Lesson Learned**: Caching essential for slow storage media; offline analysis can eliminate speculative loads

### Challenge 5: Debugging Without printf()
**Problem**: printf() and vprintf() crash in the PS1 game loop (only safe during early init)
**Solution**: Visual debugging via colored pixels (LoadImage), 5-panel telemetry overlay, debugMode=0 to disable text paths

**Lesson Learned**: Creative debugging techniques for constrained environments. Visual indicators are more useful than text output on embedded targets.

### Challenge 6: Scene Continuity Without Replay
**Problem**: Desktop engine relies on replaying prior scenes to establish state. Impractical on PS1 (CD latency, 2MB RAM).
**Solution**: Offline scene-restore pipeline. Analyze every scene offline, author restore specs that declare required resources and dirty regions, compile into C header.

**Lesson Learned**: Moving work from runtime to build time is a powerful strategy for constrained platforms.

### Challenge 7: Sprite Memory Pressure
**Problem**: 15-bit direct-color sprites consume too much RAM for multi-sprite scenes.
**Solution**: 4-bit indexed format (~4x savings), palette LUT for compositing, background tiles stay 15-bit for direct framebuffer upload.

**Lesson Learned**: Format-level optimization beats algorithmic optimization when the bottleneck is data size.

## Architecture Wins

### 1. Clean Separation of Concerns
- **Platform-independent core**: 4,000+ lines unchanged
- **Platform-specific layers**: Only 900 lines needed
- **Minimal coupling**: Only 3 files required porting

**Result**: 63% code reuse, minimal effort for new platforms

### 2. Memory Optimization First
- **4mb2025 branch**: Achieved 350KB peak usage
- **LRU cache**: With pinning for active resources
- **Lazy loading**: On-demand resource decompression

**Result**: PS1 port had 1.65MB of headroom (6x headroom!)

### 3. C99 Standards Compliance
- **No compiler extensions**: Portable across toolchains
- **Standard types**: uint8, uint16, uint32
- **Clean headers**: Minimal dependencies

**Result**: Smooth cross-compilation to MIPS

## Development Insights

### 1. Documentation Pays Off
- **1,300+ lines** of comprehensive documentation
- Clear porting strategy before coding
- API mapping tables for reference

**Result**: Structured, methodical implementation

### 2. Docker for Cross-Platform
- Avoids toolchain installation hell
- Reproducible builds everywhere
- Easy version management

**Result**: Reliable development environment

### 3. Incremental Development
- **Phase 1**: Infrastructure setup
- **Phase 2**: Build system
- **Phase 3**: Core implementation

**Result**: Trackable progress, clear milestones

### 4. Test Early, Test Often
- Created minimal test to verify toolchain
- Tested individual components before integration
- Visual debugging for quick feedback

**Result**: Fast iteration cycle

## Technical Discoveries

### 1. PS1 Hardware Advantages
- Native 640x480 support (perfect match!)
- 2MB RAM (plenty for our needs)
- Hardware sprite engine (fast rendering)

**Result**: Optimal target platform

### 2. PSn00bSDK Quality
- Modern CMake integration
- Good documentation (PDF reference)
- Active community support

**Result**: Pleasant development experience

### 3. Sprite System Complexity
- BMP parsing non-trivial (multi-image format)
- VRAM management critical (1MB limit)
- CLUT system unique to PS1

**Result**: Main implementation challenge

## Statistics

### Code Metrics (as of 2026-03-21)
```
PS1 Graphics:              3,359 lines (graphics_ps1.c)
PS1 CD-ROM I/O:            2,280 lines (cdrom_ps1.c)
PS1 Audio:                   184 lines (sound_ps1.c)
PS1 Input:                   136 lines (events_ps1.c)
Restore Pilots Header:     1,000 lines (ps1_restore_pilots.h, auto-generated)
Offline Pipeline Scripts:  8 scripts
----------------------------------------------
PS1-specific code:         ~7,000 lines

Core Engine (reused):      4,000+ lines (24 files, mostly unchanged)
```

### Binary Metrics
```
PS-EXE size:     120 KB
Text segment:    ~107 KB
Data segment:    ~10 KB
BSS:             ~57 KB
```

## Lessons for Future Ports

### What Worked Well

1. **Start from memory-optimized branch** (4mb2025)
   - Provides headroom for embedded systems
   - Proves memory management strategies

2. **Use Docker for toolchains**
   - Avoids platform-specific issues
   - Enables CI/CD easily

3. **Document before coding**
   - API mapping tables invaluable
   - Clear strategy prevents rework

4. **Visual debugging for embedded**
   - Colored screens work when printf() doesn't
   - Fast feedback loop

5. **Keep core engine pure C**
   - Maximizes portability
   - Minimizes porting effort

### What to Do Differently

1. **Test build system earlier**
   - Verify Docker image works before heavy coding
   - Catch configuration issues early

2. **Create minimal test first**
   - Verify entire toolchain with "hello world"
   - Prove boot process works

3. **Implement CD-ROM I/O earlier**
   - Critical dependency for testing
   - Blocks integration testing

4. **Profile memory continuously**
   - Monitor BSS size throughout development
   - Catch memory issues early

## Quotes from Development

> "The PS1's 2MB RAM and native 640x480 resolution make it a perfect fit for Johnny Reborn."

> "Docker solves the 'works on my machine' problem once and for all."

> "Visual debugging with colored screens is surprisingly effective when printf() doesn't work."

> "63% code reuse demonstrates the value of clean architecture."

## Future Work

### Immediate (Scene Coverage)
- [ ] Fix ACTIVITY.ADS tag 4 stale frame, promote to verified
- [ ] Unblock BUILDING.ADS and FISHING.ADS entry paths
- [ ] Expand verified scenes from 25 to 63 via cluster contract promotion

### Short-term (Completeness)
- [ ] Build automated validation harness (batch scene verification)
- [ ] Complete audio implementation (WAV to VAG, SPU playback)
- [ ] PSB/BMP hot path reconciliation

### Long-term (Polish)
- [ ] Real hardware testing
- [ ] Title screen and loading screens
- [ ] Screen effects (fade, rain)
- [ ] Memory card save/load

## Conclusion

The PS1 port boots and runs on DuckStation with 25 of 63 scenes verified correct. The offline scene-restore pipeline solved the hardest architectural problem -- eliminating replay continuity on a platform where CD latency and 2MB RAM make runtime replay impractical. The remaining work is expanding scene coverage (authored specs exist for all 63 scenes) and completing audio.

**Key Takeaway**: Moving work from runtime to build time -- scene analysis, resource declarations, dirty-region contracts -- turned an intractable runtime problem into a straightforward offline pipeline.

## See Also

- [Current Status](current-status.md) - What's done and what's next
- [Development Workflow](development-workflow.md) - How to build and test
- [Hardware Specs](hardware-specs.md) - PS1 technical details
- [API Mapping](api-mapping.md) - SDL2 → PSn00bSDK translation
