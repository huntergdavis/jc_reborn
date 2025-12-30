# Johnny Reborn - Testing Guide

This document explains the testing infrastructure for Johnny Reborn.

## Overview

The project includes a comprehensive test suite using the Unity testing framework. Tests are designed to:
- Validate core engine logic independent of SDL/graphics
- Run on embedded systems and all platform ports
- Provide regression detection during refactoring
- Execute quickly (< 1 second for full suite)

## Running Tests

**Run all tests**:
```bash
make test
```

**Run specific test suites**:
```bash
cd tests
make test-utils              # Test utility functions
make test-calcpath           # Test pathfinding algorithm
make test-resource           # Test resource loading (requires RESOURCE files)
make test-uncompress         # Test RLE/LZW decompression
make test-config             # Test configuration file I/O
make test-memory             # Memory profiling and analysis
```

## Test Suites

### Core Functionality Tests

**test_utils.c** (10 tests)
- Binary I/O functions
- Memory allocation/deallocation
- String manipulation
- Byte order conversion

**test_calcpath.c** (7 tests)
- A* pathfinding algorithm
- Edge cases (blocked paths, diagonal movement)
- Path cost calculation

**test_resource.c** (11 tests)
- Resource file parsing
- Resource lookup
- Decompression integration
- Error handling

**test_uncompress.c** (9 tests)
- RLE decompression
- LZW decompression
- Edge cases (empty data, large files)
- Format validation

**test_config.c** (11 tests)
- Command-line argument parsing
- Configuration file I/O
- Option validation

**test_memory.c** (9 tests)
- Memory profiling
- Allocation tracking
- Peak usage analysis
- Memory budget validation

### Memory Optimization Tests

```bash
cd tests
make test-disk-streaming     # Disk streaming optimization
make test-bmp-optimization   # BMP data freeing
make test-scr-optimization   # SCR data freeing
make test-lru-cache          # LRU cache with memory budget
```

### Visual Regression Tests

**Note**: Requires RESOURCE files

```bash
cd tests
# Capture reference frames from all scenes
./capture_all_reference_frames.sh

# Run visual regression tests
make test-visual-regression
```

## Test Coverage

**Current Status**: 46 passing tests

| Test Suite | Tests | Coverage |
|------------|-------|----------|
| test_utils | 10 | utils.c |
| test_calcpath | 7 | calcpath.c |
| test_resource | 11 | resource.c |
| test_uncompress | 9 | uncompress.c |
| test_config | 11 | config.c |
| test_memory | 9 | Memory profiling |

## Adding New Tests

See `tests/README.md` for detailed instructions on adding tests.

**Basic test structure**:
```c
#include "unity.h"
#include "../mymodule.h"

void setUp(void) {
    // Setup before each test
}

void tearDown(void) {
    // Cleanup after each test
}

void test_my_function(void) {
    // Test code
    TEST_ASSERT_EQUAL(expected, actual);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_my_function);
    return UNITY_END();
}
```

## Development Workflow

### General Development (main branch)

When modifying the engine:
1. **Run tests before and after changes**: `make test` to catch regressions
2. Changes to resource loading affect `resource.c/h` and require testing with RESOURCE.001
3. TTM instruction changes require understanding the bytecode format (see `ttm.c` opcode handlers)
4. Graphics changes should maintain the 640x480 screen resolution and VGA palette constraints
5. Memory-constrained ports should test against the 4MB budget limit (set via `JC_MEM_BUDGET_MB`)
6. **Add tests for new features**: Unit tests for algorithms, integration tests for workflows

### PS1 Development (ps1 branch)

**Critical PS1-specific rules**:
1. **NEVER call `CdInit()`** - BIOS already initializes CD-ROM; calling it causes crashes
2. **Use visual debugging** - printf() doesn't output to DuckStation TTY console
3. **Minimize BSS** - Use malloc() for large buffers instead of static arrays
4. **Test frequently in DuckStation** - Emulator behavior differs from other platforms
5. **Memory profiling** - Use `test_memory` to validate PS1 memory constraints

## Memory Profiling

The `test_memory` suite provides detailed analysis of memory usage:

```bash
make test-memory
```

Output includes:
- Fixed overhead (resource arrays, working memory)
- Per-scene memory usage
- Peak allocation analysis
- Optimization recommendations

**Example output**:
```
=== Estimated Memory for Typical Scene ===

Fixed overhead:
  Resource arrays: 3,368 bytes
  calcPath working: 1,448 bytes
  Subtotal: 4,816 bytes (~5KB)

Variable (per scene):
  1x TTM resource struct: ~100 bytes
  1x TTM uncompressed data: 10-50KB typical
  2-3x BMP resources: ~200 bytes each
  2-3x BMP uncompressed data: 20-100KB each
  1x SCR background: ~300KB uncompressed
  1x ADS script: 5-20KB

Typical scene total: 400-600KB
Peak (with multiple TTMs): 1-2MB

=== Optimization Opportunities ===
1. Lazy-load resources (biggest win)
2. LRU cache for decompressed data
3. Release BMP data after converting to SDL surfaces
4. Free SDL surfaces after rendering
```

## Continuous Integration

The test suite is designed to run in CI/CD pipelines:
- All tests run in < 1 second
- No external dependencies (except RESOURCE files for some tests)
- Exit code indicates pass/fail
- Compatible with all platforms

## Regression Testing

When making changes:
1. Run full test suite before changes
2. Make your modifications
3. Run full test suite after changes
4. Investigate any new failures
5. Add tests for new functionality

## Performance Testing

The benchmark utility can be used for performance testing:

```bash
./jc_reborn bench
```

This runs performance tests on critical code paths and reports timing information.
