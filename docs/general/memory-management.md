# Johnny Reborn - Memory Management

This document explains the memory management strategy used in Johnny Reborn, particularly for embedded systems with limited RAM.

## Overview

The `4mb2025` branch (and PS1 port) use **lazy resource loading with LRU caching** to dramatically reduce memory usage:
- **Before optimization**: 20MB+ peak usage
- **After optimization**: 2-4MB typical, 350KB minimum

## Memory Budget

The default memory budget is 4MB, configurable via environment variable:

```bash
export JC_MEM_BUDGET_MB=2  # Set 2MB budget for PS1
./jc_reborn
```

## Lazy Loading Strategy

Resources are decompressed **on-demand**, not at startup:

```c
// Old approach - load everything
void init() {
    for (int i = 0; i < NUM_RESOURCES; i++) {
        resources[i] = loadResource(i);  // 20MB total
    }
}

// New approach - load on-demand
void getResource(int id) {
    if (!resources[id].loaded) {
        loadResourceLazy(id);  // Only when needed
    }
    return &resources[id];
}
```

## LRU Cache with Pinning

Resources use an LRU (Least Recently Used) cache to manage memory:

**Features**:
- Active TTM/ADS scripts are **pinned** in memory to prevent eviction
- Inactive resources are evicted when memory budget is exceeded
- BMP/SCR resources are released after converting to SDL surfaces
- Each resource tracks `lastUsedTick` and `pinCount`

**Implementation**:
```c
typedef struct {
    void *data;
    uint32 lastUsedTick;
    uint32 pinCount;
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

## Memory Profiling Insights

From `test_memory.c` analysis:

### Fixed Overhead
- **Resource arrays**: ~3.4KB (resource pointer arrays)
- **calcPath working memory**: ~1.4KB
- **Total fixed**: ~5KB

### Typical Scene (400-600KB)
- **1x TTM resource**: ~100 bytes struct + 10-50KB data
- **2-3x BMP resources**: ~200 bytes each + 20-100KB data each
- **1x SCR background**: ~300KB uncompressed
- **1x ADS script**: 5-20KB

### Peak Usage
- **Multiple active TTMs**: 1-2MB
- **Complex scenes**: Up to 2MB with multiple animations

### Main Consumers
1. **Uncompressed resource data** (largest consumer)
2. **Graphics/sprite caching**
3. **Multiple TTM slots in memory simultaneously**

## Optimization Techniques

### 1. Release After Conversion
BMP/SCR resources are released after converting to SDL surfaces:
```c
// Load BMP
struct TBmpResource *bmp = resourceGetBmp("SPRITE.BMP");

// Convert to SDL surface
SDL_Surface *surface = convertBmpToSurface(bmp);

// FREE the BMP data (keep only SDL surface)
free(bmp->uncompressedData);
bmp->uncompressedData = NULL;
```

### 2. Static vs Dynamic Allocation

**Before** (large BSS section):
```c
static char buffer[32768];  // 32KB in BSS
```

**After** (heap allocation):
```c
char *buffer = malloc(32768);  // 32KB on heap
```

This is especially important for PS1, which has issues with large BSS sections (>50KB).

### 3. Structure Packing

**Before** (12 bytes with padding):
```c
struct Data {
    char a;     // 1 byte + 3 padding
    int b;      // 4 bytes
    char c;     // 1 byte + 3 padding
};
```

**After** (6 bytes packed):
```c
struct Data {
    int b;      // 4 bytes
    char a;     // 1 byte
    char c;     // 1 byte
} __attribute__((packed));
```

### 4. Reduce Bit Depth

**Before** (32-bit RGBA):
```c
typedef struct {
    uint32 *pixels;  // 640x480 = 1.2MB
} Surface32;
```

**After** (8-bit indexed):
```c
typedef struct {
    uint8 *pixels;         // 640x480 = 300KB
    uint16 palette[256];   // + 512 bytes
} Surface8;  // Total: 300.5KB (4x smaller!)
```

## Platform-Specific Considerations

### PS1 (2MB RAM)
- Memory budget: 350KB-2MB
- Use malloc() for buffers >1KB
- Keep BSS < 50KB
- Pin active animations
- LRU eviction for sprites

### Dreamcast (16MB RAM)
- Memory budget: 4-8MB
- Can be more aggressive with caching
- Pre-load frequently used resources

### RetroFW (32-64MB RAM)
- Memory budget: 8-16MB
- Can cache entire resource set if desired

## Memory Analysis Tools

Run memory profiling:
```bash
make test-memory
```

This produces detailed output:
- Fixed overhead breakdown
- Typical scene estimates
- Peak usage scenarios
- Optimization recommendations

## Debugging Memory Issues

**Symptoms of memory problems**:
- Crashes during scene transitions
- Slow performance
- Failed resource loading
- System freezes

**Debugging steps**:
1. Run `test_memory` to see current usage
2. Check memory budget: `echo $JC_MEM_BUDGET_MB`
3. Enable debug output: `./jc_reborn debug`
4. Profile with valgrind: `valgrind --leak-check=full ./jc_reborn`
5. Check resource pinning logic in `resource.c`

## Best Practices

1. **Always profile before optimizing** - Use `test_memory` to identify actual bottlenecks
2. **Pin active resources** - Prevent eviction of currently-used data
3. **Release after conversion** - Don't keep both compressed and uncompressed data
4. **Use heap for large buffers** - Keep BSS small, especially for PS1
5. **Test on target hardware** - Emulators may mask memory issues
6. **Set appropriate budgets** - Match target platform's RAM constraints

## See Also

- [Architecture](architecture.md) - Overall system design
- [Testing Guide](testing-guide.md) - Memory testing procedures
- PS1 documentation for platform-specific memory constraints
