# Embedded Debug Workflow Skill

Debug workflow for embedded systems when traditional debuggers are unavailable or limited.

## Usage

Run this skill when debugging crashes, graphical glitches, or unexpected behavior on embedded platforms.

## Task

### 1. Establish Debug Output

#### Serial/UART Logging
```c
#ifdef PS1_BUILD
#include <psxapi.h>
#define DEBUG_LOG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_LOG(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#endif

// Usage
DEBUG_LOG("Initializing graphics: %dx%d", width, height);
DEBUG_LOG("Loading resource %d: %s", id, name);
```

#### Visual Debug Output
```c
// Draw debug text on screen
void debugText(int x, int y, const char *text) {
#ifdef PS1_BUILD
    FntPrint(-1, text);  // PSn00bSDK debug font
#else
    // SDL text rendering
#endif
}

// Show memory usage on-screen
void debugOverlay() {
    char buf[64];
    sprintf(buf, "MEM: %d KB", getCurrentMemory() / 1024);
    debugText(10, 10, buf);
}
```

#### LED Blink Codes
```c
// For platforms with minimal output
void blinkError(int code) {
    for (int i = 0; i < code; i++) {
        LED_ON();
        delay(100);
        LED_OFF();
        delay(100);
    }
}
```

### 2. Common Debug Patterns

#### Checkpoint Markers
```c
enum Checkpoint {
    CP_START = 1,
    CP_INIT_GRAPHICS = 2,
    CP_LOAD_RESOURCES = 3,
    CP_MAIN_LOOP = 4,
    CP_RENDER = 5
};

int lastCheckpoint = 0;

#define CHECKPOINT(cp) do { \
    lastCheckpoint = cp; \
    DEBUG_LOG("Checkpoint: %d", cp); \
} while(0)

// Usage
CHECKPOINT(CP_INIT_GRAPHICS);
grInit(640, 480, 0);
CHECKPOINT(CP_LOAD_RESOURCES);
```

#### Assert Macros
```c
#ifdef DEBUG_BUILD
#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        DEBUG_LOG("ASSERT FAILED: %s at %s:%d", \
                  msg, __FILE__, __LINE__); \
        while(1); /* Hang for inspection */ \
    } \
} while(0)
#else
#define ASSERT(cond, msg)
#endif

// Usage
ASSERT(ptr != NULL, "Null pointer");
ASSERT(width > 0, "Invalid width");
```

#### Boundary Checks
```c
#define SAFE_ARRAY_ACCESS(arr, idx, size) do { \
    ASSERT((idx) >= 0 && (idx) < (size), \
           "Array bounds violation"); \
    return (arr)[(idx)]; \
} while(0)
```

### 3. Emulator-Specific Debugging

#### DuckStation (PS1)
- **Settings**: Enable debug console
- **TTY Output**: View printf() output
- **CPU Debugger**: Set breakpoints, inspect registers
- **VRAM Viewer**: Inspect textures in VRAM
- **Memory Card**: Enable save states

#### Redream/lxdream (Dreamcast)
- **GDB Support**: Remote debugging
- **Frame capture**: Analyze rendering
- **Performance profiler**: CPU/GPU timing

#### RetroArch
- **Core options**: Enable verbose logging
- **Fast-forward**: Speed up testing
- **Save states**: Quick iteration

### 4. Systematic Bug Hunting

#### Binary Search Isolation
```c
// Progressively disable features to isolate bug
void gameLoop() {
    // STEP 1: Comment out everything
    // return;

    // STEP 2: Enable graphics only
    // grUpdateDisplay();
    // return;

    // STEP 3: Enable input
    // eventsWaitTick(1);
    // grUpdateDisplay();
    // return;

    // STEP 4: Full game logic
    updateGame();
    eventsWaitTick(1);
    grUpdateDisplay();
}
```

#### Minimal Reproduction
```c
// Create minimal test case
void testMinimal() {
    grInit(640, 480, 0);

    // Just test the specific failing operation
    Surface *sfc = grLoadBmp("test.bmp");
    grDrawSprite(sfc, 100, 100);
    grRefreshDisplay();

    // Hang to inspect
    while(1);
}
```

### 5. Performance Profiling

#### Frame Time Measurement
```c
uint32 frameStartTime;
uint32 frameCount = 0;

void frameStart() {
    frameStartTime = getCurrentTicks();
}

void frameEnd() {
    uint32 elapsed = getCurrentTicks() - frameStartTime;
    frameCount++;

    if (frameCount % 60 == 0) {
        DEBUG_LOG("Frame time: %d ms (%.1f FPS)",
                  elapsed, 1000.0f / elapsed);
    }
}
```

#### Function Timing
```c
#define TIME_FUNCTION(name, code) do { \
    uint32 start = getCurrentTicks(); \
    code; \
    uint32 elapsed = getCurrentTicks() - start; \
    DEBUG_LOG("%s took %d ms", name, elapsed); \
} while(0)

// Usage
TIME_FUNCTION("Load BMP", {
    sfc = grLoadBmp("sprite.bmp");
});
```

### 6. Memory Corruption Detection

#### Guard Bytes
```c
#define GUARD_MAGIC 0xDEADBEEF

typedef struct {
    uint32 guardFront;
    char data[256];
    uint32 guardBack;
} GuardedBuffer;

void checkGuards(GuardedBuffer *buf) {
    ASSERT(buf->guardFront == GUARD_MAGIC, "Front guard corrupted");
    ASSERT(buf->guardBack == GUARD_MAGIC, "Back guard corrupted");
}
```

#### Poison Memory
```c
void* debugMalloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr) {
        memset(ptr, 0xCC, size);  // Poison value
    }
    return ptr;
}

void debugFree(void *ptr, size_t size) {
    memset(ptr, 0xDD, size);  // Dead memory marker
    free(ptr);
}
```

### 7. Visual Regression Testing

```c
// Capture frame to file
void captureFrame(const char *filename) {
#ifdef PS1_BUILD
    // Dump VRAM region to file
    saveVRAMRegion(filename, 0, 0, 640, 480);
#else
    // SDL screenshot
    SDL_SaveBMP(screen, filename);
#endif
}

// Compare with reference
int compareFrames(const char *test, const char *reference) {
    // Pixel-by-pixel comparison
    // Return number of different pixels
}
```

## Debug Checklist

- [ ] Serial/debug output enabled
- [ ] Checkpoint logging added
- [ ] Assertions in critical paths
- [ ] Boundary checks enabled
- [ ] Emulator debugger configured
- [ ] Minimal reproduction created
- [ ] Performance profiled
- [ ] Memory guards in place
- [ ] Visual regression captures

## Common Issues and Solutions

**Symptom**: Crash on startup
- Add checkpoints to find exact location
- Check stack size in linker script
- Verify BSS section cleared

**Symptom**: Graphics corruption
- Check VRAM allocation overlap
- Verify DMA transfer complete
- Inspect VRAM viewer in emulator

**Symptom**: Intermittent crashes
- Add memory guards
- Check for race conditions
- Enable all assertions

**Symptom**: Slow performance
- Profile frame time
- Check for blocking operations
- Reduce debug logging

## Success Criteria

- Bug reproducible consistently
- Debug output provides useful info
- Root cause identified
- Fix verified in emulator
- Regression tests added
