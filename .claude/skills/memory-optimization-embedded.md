# Memory Optimization for Embedded Systems Skill

Analyze and optimize memory usage for constrained embedded platforms (PS1: 2MB, Dreamcast: 16MB, etc.).

## Usage

Run this skill when memory usage exceeds target platform limits or when profiling embedded builds.

## Task

### 1. Memory Analysis

#### Measure Current Usage
```c
// Add to test suite
#include <stdlib.h>

size_t getCurrentMemoryUsage() {
#ifdef PS1_BUILD
    // PS1-specific memory tracking
    extern char __heap_start;
    extern char __heap_end;
    return (size_t)(&__heap_end - &__heap_start);
#else
    // Use platform-specific API
#endif
}
```

#### Profile Allocations
- Track malloc/calloc calls
- Measure static data size (`size <executable>`)
- Identify memory hotspots (resource loading, graphics buffers)
- Create memory budget spreadsheet

### 2. Common Memory Consumers

**Large Arrays/Buffers:**
- Resource caches
- Decompression buffers
- Graphics framebuffers
- Sound mixing buffers

**Dynamic Allocations:**
- Uncompressed resource data
- Animation frame buffers
- String buffers
- Path finding arrays

### 3. Optimization Techniques

#### Lazy Loading
```c
// Before: Load all at startup
void init() {
    for (int i = 0; i < NUM_RESOURCES; i++) {
        resources[i] = loadResource(i);  // 20MB total
    }
}

// After: Load on-demand
void getResource(int id) {
    if (!resources[id].loaded) {
        loadResourceLazy(id);  // Only when needed
    }
    return &resources[id];
}
```

#### LRU Caching
```c
typedef struct {
    void *data;
    int lastUsed;
    int isPinned;  // Prevent eviction
} CacheEntry;

void evictOldest(void) {
    int oldestIdx = -1;
    int oldestTime = INT_MAX;

    for (int i = 0; i < CACHE_SIZE; i++) {
        if (!cache[i].isPinned && cache[i].lastUsed < oldestTime) {
            oldestIdx = i;
            oldestTime = cache[i].lastUsed;
        }
    }

    if (oldestIdx >= 0) {
        free(cache[oldestIdx].data);
        cache[oldestIdx].data = NULL;
    }
}
```

#### Reduce Bit Depth
```c
// Before: 32-bit RGBA (4 bytes/pixel)
typedef struct {
    uint32 *pixels;  // 640x480 = 1.2MB
} Surface32;

// After: 8-bit indexed (1 byte/pixel)
typedef struct {
    uint8 *pixels;   // 640x480 = 300KB
    uint16 palette[256];  // + 512 bytes = 300.5KB total
} Surface8;
```

#### Static vs Dynamic
```c
// Before: Dynamic allocation
char *buffer = malloc(BUFFER_SIZE);  // Heap + overhead

// After: Static allocation (if size known)
static char buffer[BUFFER_SIZE];     // BSS section, no overhead
```

#### Pack Structures
```c
// Before: 12 bytes (padding)
struct Data {
    char a;     // 1 byte + 3 padding
    int b;      // 4 bytes
    char c;     // 1 byte + 3 padding
};

// After: 6 bytes (packed)
struct Data {
    int b;      // 4 bytes
    char a;     // 1 byte
    char c;     // 1 byte
} __attribute__((packed));
```

### 4. Platform-Specific Optimizations

#### PS1 (2MB RAM, 1MB VRAM)
- Use VRAM for textures (don't keep in RAM)
- 4-bit or 8-bit indexed textures
- Streaming from CD for large assets
- Small fixed-size buffers

#### Dreamcast (16MB RAM, PVR VRAM)
- Texture compression (VQ, twiddled)
- Use PVR VRAM for graphics
- More aggressive caching possible
- DMA transfers from CD

#### RetroFW (32-64MB RAM)
- More relaxed constraints
- Can cache more resources
- Focus on load times over memory

### 5. Measurement and Validation

#### Create Test Suite
```c
void testMemoryProfile() {
    size_t before = getCurrentMemoryUsage();

    // Simulate typical usage
    loadInitialResources();
    playScene();

    size_t after = getCurrentMemoryUsage();
    size_t peak = getPeakMemoryUsage();

    printf("Memory usage:\n");
    printf("  Baseline: %zu KB\n", before / 1024);
    printf("  Current:  %zu KB\n", after / 1024);
    printf("  Peak:     %zu KB\n", peak / 1024);

    assert(peak < MEMORY_BUDGET);
}
```

#### Compile with Memory Reporting
```bash
# Show section sizes
mipsel-none-elf-size executable.elf

# Detailed memory map
mipsel-none-elf-nm -S --size-sort executable.elf | head -20

# Link with memory map
mipsel-none-elf-gcc -Wl,-Map=output.map
```

### 6. Memory Budget Template

```markdown
## Memory Budget (PS1 - 2MB Total)

### Fixed Allocations
- Stack:               64 KB
- Heap:                256 KB
- Code + static data:  200 KB
- Graphics buffers:    600 KB (2x framebuffer)
- Sound buffers:       128 KB
- Reserved/safety:     50 KB
**Fixed Total:**       1298 KB

### Dynamic Pool
- Resource cache:      722 KB (remaining)
  - 1x Screen (SCR):   ~300 KB
  - 1x TTM script:     ~50 KB
  - 2x BMP sprites:    ~200 KB each
  - Headroom:          ~72 KB

### VRAM Budget (1MB)
- Framebuffer 0:       300 KB
- Framebuffer 1:       300 KB
- Texture cache:       400 KB
- CLUT palettes:       24 KB
```

## Checklist

- [ ] Baseline memory measured
- [ ] Memory budget defined
- [ ] Hotspots identified
- [ ] Optimization targets selected
- [ ] Changes implemented
- [ ] Memory usage validated
- [ ] Performance impact measured
- [ ] Documentation updated

## Success Criteria

- Peak memory under budget
- No memory leaks detected
- Load times acceptable
- Visual quality maintained
- Test suite passes
