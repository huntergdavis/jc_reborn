# SDL-Compat Lite Specification

Date: 2026-03-17
Status: Working contract

## Purpose

Define the narrow graphics/runtime surface the PS1 path must satisfy so gameplay
code can target stable semantics instead of PS1-specific replay and recovery
workarounds.

This contract is intentionally smaller than full SDL. It describes what Johnny's
ADS/TTM/walk code actually uses today.

## Contract surface

### Layer lifecycle

Functions:

- `grNewLayer()`
- `grFreeLayer()`
- `grLoadScreen()`
- `grInitEmptyBackground()`

Required behavior:

- gameplay can acquire a logical per-thread layer
- layers persist across frames until explicitly released
- background load establishes the clean scene base for subsequent draw/restore
- layer allocation and release are implementation details, not gameplay state

### Frame lifecycle

Functions:

- `grBeginFrame()` on PS1
- `grUpdateDisplay()`

Required behavior:

- runtime starts a frame from a deterministic base state
- current PS1 implementation achieves this with clean-background tile copies plus
  dirty-row restore, not full-frame rebuild every tick
- runtime presents layers in deterministic order:
  - background
  - saved/restored zone overlay if used
  - active ADS/TTM thread layers in slot order
  - holiday/extra layer last
- gameplay must not know whether present uses SDL blits, RAM compositing, or GPU
  primitives

### Sprite drawing

Functions:

- `grLoadBmp()`
- `grReleaseBmp()`
- `grDrawSprite()`
- `grDrawSpriteFlip()`

Required behavior:

- sprite draw uses transparent blit semantics
- flipped draw is behaviorally equivalent to horizontal flip of the same source
- sprite resources are loaded/released by slot, not by caller-managed surfaces
- caller-visible semantics are position, image selection, and ordering only
- the runtime may satisfy this through either legacy BMP decode or offline
  transcoded PSB sprite bundles; that choice must not change gameplay-visible
  semantics

### Primitive drawing

Functions:

- `grDrawPixel()`
- `grDrawLine()`
- `grDrawRect()`
- `grDrawCircle()`
- `grClearScreen()`

Required behavior:

- primitives apply to the current logical target/layer semantics
- `grClearScreen()` clears the current logical layer, not global gameplay state
- implementations may choose software or hardware paths, but results must remain
  deterministic for the script patterns that exist

### Region save/restore

Functions:

- `grCopyZoneToBg()`
- `grSaveImage1()`
- `grSaveZone()`
- `grRestoreZone()`

Required behavior:

- runtime supports the current script pattern of saving and later restoring a
  bounded region
- gameplay does not need to know whether restore comes from a separate saved
  layer, clean background tiles, or offline templates
- restore behavior must not depend on replay-record resurrection

### Clip / offsets

Functions:

- `grSetClipZone()`
- global offsets `grDx`, `grDy`

Required behavior:

- gameplay can request bounded drawing regions where scripts rely on it
- coordinate offsets remain a runtime presentation concern, not gameplay logic

## Out of contract

These are implementation details and should not leak back into gameplay logic:

- replay records
- actor continuity matching
- handoff replay injection
- background tile management
- dirty-row tracking / partial tile upload
- pack lookup and prefetch
- PSB registry lookup
- BMP/TTM caching policy

## Current gameplay dependency map

Direct graphics call sites in gameplay/runtime code:

- `ttm.c`
  - clip zone
  - copy/save/restore zone
  - pixel/line/rect/circle
  - draw sprite / draw sprite flip
  - clear screen
  - load screen
  - load BMP
- `walk.c`
  - clear screen
  - draw sprite / draw sprite flip
- `island.c`
  - load screen
  - load BMP / release BMP
  - draw sprite / draw sprite flip
- `ads.c`
  - layer allocation/release
  - frame begin/present
  - restore background tiles
  - replay sprite path (current workaround, not contract)

## Gap matrix

Legend:

- `Complete`: behavior exists and matches the current contract closely enough
- `Partial`: behavior exists but is implemented through PS1-specific machinery or
  only covers current script patterns
- `Stub`: declared but not meaningfully implemented on PS1
- `Leak`: gameplay currently depends on non-contract implementation details

| Area | PC SDL path | PS1 path | Status | Notes |
| --- | --- | --- | --- | --- |
| Layer allocation/release | Yes | Yes | Complete | Both paths expose logical layers. |
| Background load/base scene | Yes | Yes | Complete | PS1 uses clean background tiles internally. |
| Frame present ordering | Yes | Yes | Complete | PS1 order is deterministic, though implemented differently. |
| Frame begin/reset | Implicit | Explicit | Partial | PS1 requires `grBeginFrame()`; contract should standardize this lifecycle. |
| Draw sprite | Yes | Yes | Complete | PS1 path is pack-backed and authoritative now; sprite source may be BMP or PSB. |
| Draw sprite flip | Yes | Yes | Complete | Same semantics, different backend. |
| Transparent blit semantics | Yes | Yes | Partial | Works on current paths; still split across BMP and PSB backends and needs semantic convergence. |
| Load/release BMP by slot | Yes | Yes | Complete | Runtime implementation differs, caller contract matches. |
| Draw rect | Yes | Yes | Partial | PS1 uses optimized software tile writes and dirty-row tracking instead of SDL fill; good for current script usage. |
| Draw pixel | Yes | Yes | Complete | Present on both. |
| Draw line | Yes | Weak | Partial | PS1 is still effectively stub/cosmetic for now. |
| Draw circle | Yes | Weak | Partial | PS1 path is limited and should be validated against actual script usage. |
| Set clip zone | Yes | Minimal | Partial | Exists on PS1 but needs tighter semantic validation. |
| Copy zone to background | Yes | Yes | Partial | PS1 now commits the current rectangle into the clean background restore baseline instead of keeping a separate saved-zones overlay layer. |
| Save image1 | Minimal | Yes | Partial | PS1 now routes it through the same bounded clean-background commit behavior as `COPY_ZONE_TO_BG`, which matches current observed usage better than a stub. |
| Save zone | Yes | Yes | Partial | PS1 now tracks one active zone, matching current script assumptions. |
| Restore zone | Yes | Yes | Partial | PS1 now restores from clean background tiles, but only for the simple active-zone pattern. |
| Clear screen | Yes | Divergent | Partial | PS1 intentionally suppresses some clears to avoid blinking; this needs to become offline/runtime policy, not gameplay-visible behavior. |
| Replay sprite | N/A | Yes | Leak | Legacy PS1-only workaround, now a shrinking boundary rather than the main render architecture. |
| Actor continuity / recovery injection | N/A | Yes | Leak | Must continue moving out of gameplay-visible correctness path. |
| Dirty-row tile restore/upload | N/A | Yes | Implementation detail | Current renderer optimization boundary; must stay invisible to gameplay semantics. |
| PSB sprite path | N/A | Yes | Partial | Good fit for the target architecture, but still needs route-by-route convergence with legacy BMP behavior. |

## Kill list for long-standing bug sources

These are the highest-value leaks to remove as the new architecture advances:

1. `ads.c` dependence on replay carry/merge/recover for visual correctness.
2. PS1-only interpretation of `CLEAR_SCREEN` to suppress blinking.
3. PS1 frame correctness depending on remembered sprite identity rather than
   explicit restore data.
4. Remaining sprite-path divergence where PSB and BMP do not yet behave identically.
5. Remaining stubbed or partial zone/image operations that force unrelated replay behavior to carry
   correctness.

## Immediate next implementation targets

1. Replace `CLEAR_SCREEN` divergence with explicit restore/template policy on the
   pilot route.
2. Emit offline dirty-region templates for a pilot family and consume them from
   pack-backed runtime data.
3. Reduce `ads.c` replay continuity on the pilot path until it is no longer a
   correctness dependency.
