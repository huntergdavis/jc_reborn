# Platform Abstraction Layer Design Skill

Design and implement clean platform abstraction layers for multi-platform C projects.

## Usage

Run this skill when porting a SDL/desktop application to embedded systems or creating cross-platform code.

## Design Principles

1. **Separate Platform-Specific Code**
   - Core engine: Pure C, no platform dependencies
   - Platform layer: SDL, PS1, Dreamcast, etc.
   - Interface: Common header with function signatures

2. **Conditional Compilation**
   - Use preprocessor defines (PS1_BUILD, DREAMCAST_BUILD, etc.)
   - Conditional includes in main files
   - Avoid #ifdef inside core logic

3. **Naming Convention**
   - Core: `graphics.c`, `sound.c`, `events.c`
   - Platform: `graphics_ps1.c`, `sound_sdl.c`, `events_dreamcast.c`
   - Headers: `graphics.h` (shared interface)

## Task Steps

### 1. Identify Platform Dependencies

Scan codebase for:
- SDL includes (`<SDL2/SDL.h>`)
- Platform-specific types (`SDL_Surface`, `FILE`)
- OS-specific functions (`fopen`, `malloc`)
- Graphics APIs (OpenGL, DirectX)

### 2. Design Common Interface

Create shared header with:
```c
// graphics.h (shared interface)
#ifndef GRAPHICS_H
#define GRAPHICS_H

#include "mytypes.h"

// Platform-agnostic types
typedef struct Surface Surface;

// Common function signatures
void grInit(int width, int height, int fullscreen);
void grRefreshDisplay(void);
Surface *grLoadBmp(const char *filename);
void grDrawSprite(Surface *sfc, int x, int y);
void grEnd(void);

#endif
```

### 3. Implement Platform Layers

For each platform, create `graphics_<platform>.c`:

```c
// graphics_ps1.c
#include <psxgpu.h>
#include "graphics.h"

typedef struct Surface {
    uint16 width;
    uint16 height;
    uint16 vramX;
    uint16 vramY;
} Surface;

void grInit(int width, int height, int fullscreen) {
    // PS1-specific GPU initialization
    ResetGraph(0);
    // ...
}

// Implement other functions...
```

### 4. Update Build System

**CMakeLists.txt approach:**
```cmake
if(PS1_BUILD)
    set(PLATFORM_SOURCES
        graphics_ps1.c
        sound_ps1.c
        events_ps1.c
    )
    add_definitions(-DPS1_BUILD)
else()
    set(PLATFORM_SOURCES
        graphics_sdl.c
        sound_sdl.c
        events_sdl.c
    )
endif()
```

**Conditional includes approach:**
```c
// main.c
#ifdef PS1_BUILD
#include "graphics_ps1.h"
#include "sound_ps1.h"
#include "events_ps1.h"
#else
#include "graphics.h"
#include "sound.h"
#include "events.h"
#endif
```

### 5. Handle Platform-Specific Types

For types that cross platform boundaries:

```c
// mytypes.h
#ifdef PS1_BUILD
typedef uint8  bool;
#define true  1
#define false 0
#else
#include <stdbool.h>
#endif
```

## Refactoring Checklist

- [ ] Core engine code has zero platform-specific includes
- [ ] All SDL/platform code isolated to platform files
- [ ] Common interface defined in shared header
- [ ] Build system supports multiple platforms
- [ ] Conditional compilation tested
- [ ] Documentation updated (PORTING.md)

## Common Patterns

### Pattern 1: Forward Declarations
```c
// Avoid including stdio.h in headers
typedef struct _FILE FILE;
FILE *safe_fopen(const char *path, const char *mode);
```

### Pattern 2: Platform-Specific Initialization
```c
void platformInit(void) {
#ifdef PS1_BUILD
    ResetGraph(0);
    InitPAD(pad_buff[0], 34, pad_buff[1], 34);
#else
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
#endif
}
```

### Pattern 3: Type Aliases
```c
#ifdef PS1_BUILD
typedef PS1Surface Surface;
#else
typedef SDL_Surface Surface;
#endif
```

## Benefits

- Clean separation of concerns
- Easy to add new platforms
- Core engine remains portable
- Platform-specific optimizations possible
- Easier testing (can mock platform layer)

## Success Criteria

- Core engine compiles on all platforms
- Platform layers compile independently
- No platform-specific code in core files
- Build system switches platforms cleanly
- Documentation explains architecture
