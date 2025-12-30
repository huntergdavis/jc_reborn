# Johnny Reborn - System Architecture

This document describes the core architecture of the Johnny Reborn engine.

## Overview

Johnny Reborn is an open-source C engine that recreates the classic "Johnny Castaway" screensaver from 1992. It uses SDL2 to play the adventures of Johnny on his desert island by interpreting original Sierra ScreenAntics data files (RESOURCE.MAP and RESOURCE.001).

## Data Pipeline

The engine parses Sierra's proprietary resource format:

1. **Resource Loading** (`resource.c/h`): Parses RESOURCE.MAP index file, locates compressed resources in RESOURCE.001
2. **Decompression** (`uncompress.c/h`): LZ77 decompression for packed data
3. **Resource Types**: ADS (scene scripts), TTM (animation scripts), BMP (sprite sheets), SCR (backgrounds), PAL (palettes)

## Execution Flow

- **Story Mode** (`story.c/h`): Top-level orchestrator that randomly selects scenes and manages Johnny's day-to-day activities
- **ADS Engine** (`ads.c/h`): Scene script interpreter that coordinates multiple TTM animations and manages scene timing/transitions
- **TTM Engine** (`ttm.c/h`): Animation bytecode interpreter with instruction set for sprite control, timing, drawing operations
- **Walk System** (`walk.c/h`, `calcpath.c/h`): Pathfinding for Johnny's transitional walks between scenes

## Rendering System

- **Graphics** (`graphics.c/h`): SDL2 wrapper managing screen buffer, sprite blitting, drawing primitives, and palette operations
- **Island Rendering** (`island.c/h`): Procedurally generates the island landmass with random placement and clouds
- **Multi-layer Compositing**: Background layer, TTM animation layers (up to 10 threads), sprite layers (6 BMP slots with 120 sprites each)

## Memory Management

The current branch uses **lazy resource loading with LRU caching**:
- Resources are decompressed on-demand, not at startup
- Active TTM/ADS scripts are pinned in memory to prevent eviction
- BMP/SCR resources are released after converting to SDL surfaces
- Default memory budget is 4MB (configurable via `JC_MEM_BUDGET_MB` environment variable)
- This optimization reduces peak memory from 20MB+ to 2-4MB for typical scenes

**Memory profiling insights**:
- **Fixed overhead**: ~5KB (resource arrays + calcPath working memory)
- **LZW decompression**: ~16KB working memory during decompress
- **Typical scene**: 400-600KB (1x TTM, 2-3x BMP, 1x SCR, 1x ADS)
- **Peak usage**: 1-2MB with multiple active TTMs
- **Main optimization targets**:
  1. Uncompressed resource data (largest consumer)
  2. Graphics/sprite caching
  3. Multiple TTM slots in memory simultaneously

Run `make test-memory` to see detailed memory analysis.

## Configuration & Events

- **Config** (`config.c/h`): Command-line argument parsing
- **Events** (`events.c/h`): SDL event handling for keyboard/window events
- **Sound** (`sound.c/h`): WAV file playback for sound effects
- **Bench** (`bench.c/h`): Performance testing utilities

## Key Data Structures

- `struct TTtmSlot`: Holds TTM bytecode, sprite sheets, and parsed tag table
- `struct TTtmThread`: Runtime state for executing TTM animation (IP, timers, drawing state)
- `struct TAdsScene`: Scene definition with slot/tag/repeat count
- Resource structs (`TAdsResource`, `TBmpResource`, `TScrResource`, `TTtmResource`): Parsed resource headers with compression metadata

## File Organization

**Unchanged Files** (Core Engine - ~4000 lines):
These pure C files need no modifications for platform ports:
```
✅ jc_reborn.c      - Main game loop
✅ utils.c/h        - Utility functions
✅ uncompress.c/h   - LZW/RLE decompression
✅ resource.c/h     - Resource loading
✅ dump.c           - Debug output
✅ story.c/h        - Story orchestration
✅ walk.c/h         - Walk animations
✅ calcpath.c/h     - A* pathfinding
✅ ads.c/h          - ADS scene interpreter
✅ ttm.c/h          - TTM animation bytecode
✅ island.c/h       - Procedural island generation
✅ bench.c/h        - Performance benchmarking
✅ config.c/h       - Configuration parser
```

**Platform-Specific Files**:
```
SDL Implementation → Platform Implementation
------------------   -----------------------
graphics.c        → graphics_<platform>.c
sound.c           → sound_<platform>.c
events.c          → events_<platform>.c
```

For more details on memory management, see [Memory Management](memory-management.md).
For platform ports, see [Branch Structure](branch-structure.md).
