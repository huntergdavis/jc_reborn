# Johnny Reborn Test Suite

Comprehensive test suite for the Johnny Reborn engine using the Unity testing framework.

## Overview

This test suite validates the core engine components to ensure reliability across multiple platform ports (Dreamcast, Xbox, embedded systems, etc.). Tests focus on:

- **Unit tests**: Individual functions and modules
- **Integration tests**: Resource loading, parsing, and decompression
- **Regression tests**: Ensuring refactors don't break existing functionality

## Running Tests

### Quick Start

From the root project directory:
```bash
make test
```

From the tests directory:
```bash
cd tests
make
```

### Individual Test Suites

```bash
# Run individual test suites
make test-utils       # Utils tests
make test-calcpath    # Pathfinding tests
make test-resource    # Resource tests (requires RESOURCE files)
make test-uncompress  # Decompression tests
make test-config      # Config file tests
make test-memory      # Memory profiling tests
```

### Clean Test Builds

```bash
# From root directory
make test-clean

# From tests directory
make clean
```

## Test Suites

### test_utils.c
Tests utility functions used throughout the engine:
- Memory allocation (`safe_malloc`)
- Binary file reading (little-endian uint8/16/32)
- String parsing
- Data block reading

**Coverage**: 10 tests covering utils.c functions

### test_calcpath.c
Tests the pathfinding algorithm for Johnny's walks between island locations:
- Path calculation between nodes
- Path validation and termination
- Deterministic behavior with fixed random seeds
- Boundary conditions

**Coverage**: 7 tests covering calcpath.c algorithm

### test_resource.c
Tests resource loading, parsing, and lookup:
- RESOURCE.MAP/RESOURCE.001 parsing
- TTM, ADS, BMP, SCR, PAL resource lookup
- Resource structure validation
- Decompression integrity

**Requirements**: Needs `RESOURCE.MAP` and `RESOURCE.001` in `jc_resources/` directory
**Coverage**: 11 tests (skipped if resource files not found)

### test_uncompress.c
Tests RLE and LZW decompression algorithms:
- Invalid compression method handling
- RLE literal runs and repeated bytes
- RLE mixed runs and edge cases
- LZW simple data decompression
- Compression method selection

**Coverage**: 9 tests covering both compression methods

### test_config.c
Tests configuration file I/O:
- Write and read config values
- Non-existent file handling
- Zero, negative, and large values
- Multiple write/read cycles
- Partial config files
- Boundary value testing

**Coverage**: 11 tests covering all config operations

### test_memory.c
Memory profiling and analysis tests:
- Documents allocation sizes for each subsystem
- Measures RLE/LZW decompression overhead
- Analyzes static vs dynamic memory usage
- Estimates typical scene memory footprint
- Identifies optimization opportunities

**Coverage**: 9 tests providing memory insights

**Key Findings**:
- LZW decompress uses ~16KB working memory
- Typical scene: 400-600KB
- Peak with multiple TTMs: 1-2MB
- Main optimization targets: uncompressed resource data, graphics caching

## Framework

Tests use the **Unity** testing framework:
- Lightweight and portable (embedded-friendly)
- Simple assertion macros
- Works on all target platforms
- No external dependencies beyond standard C library

Unity files are in `tests/unity/`:
- `unity.c` - Framework implementation
- `unity.h` - Public API
- `unity_internals.h` - Internal definitions

## Adding New Tests

1. Create a new test file: `tests/test_<module>.c`
2. Include Unity header and module headers:
```c
#include "unity/unity.h"
#include "../mytypes.h"
#include "../<module>.h"
```

3. Implement setUp/tearDown and test functions:
```c
void setUp(void) {
    // Called before each test
}

void tearDown(void) {
    // Called after each test
}

void test_myFunction_works(void) {
    int result = myFunction(5);
    TEST_ASSERT_EQUAL_INT(42, result);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_myFunction_works);
    return UNITY_END();
}
```

4. Add to `tests/Makefile`:
```make
TEST_NEW = test_new
ALL_TESTS = $(TEST_UTILS) $(TEST_CALCPATH) $(TEST_RESOURCE) $(TEST_NEW)

$(TEST_NEW): test_new.c $(UNITY_OBJ) $(NEW_OBJ) $(DEPS)
	$(CC) $(CFLAGS) $^ -o $@
```

5. Add to test runner in Makefile `all` target

## Test Status

| Module | Tests | Status | Notes |
|--------|-------|--------|-------|
| utils.c | 10 | ✅ Pass | Binary I/O, memory, strings |
| calcpath.c | 7 | ✅ Pass | Pathfinding algorithm |
| resource.c | 11 | ⚠️ Conditional | Requires RESOURCE files |
| uncompress.c | 9 | ✅ Pass | RLE/LZW decompression |
| config.c | 11 | ✅ Pass | Config file read/write |
| memory profiling | 9 | ✅ Pass | Memory usage analysis |

**Total: 46 passing tests** (57 including conditional resource tests)

## Future Tests

Planned test coverage:
- **TTM interpreter**: Bytecode execution tests (requires SDL mocking)
- **ADS engine**: Scene playback integration tests (requires SDL mocking)
- **Graphics**: Pixel-level rendering validation (with mocks)
- **walk.c**: Walk transition tests
- **story.c**: Scene selection logic tests
- **Platform-specific**: Tests for low-memory behavior, endianness
- **Benchmark tests**: Performance regression detection

## CI/CD Integration

Tests are designed to run in continuous integration:
```bash
# Build and test in one command
make clean && make && make test
```

Exit codes:
- `0`: All tests passed
- Non-zero: Test failures or build errors

## Debugging Tests

Run tests with GDB:
```bash
gdb ./test_utils
(gdb) run
```

Enable debug output in tested modules:
```c
extern int debugMode;
debugMode = 1;  // In setUp() function
```

## Platform Considerations

### Embedded Systems
- Unity framework is embedded-friendly (no malloc, minimal dependencies)
- Tests can run on target hardware via serial console
- Use `make test-<module>` to run specific tests with limited memory

### Cross-Platform
- All file I/O uses stdio (portable)
- Tests avoid platform-specific code
- Binary format tests verify little-endian assumptions

### Resource Requirements
- Minimal: tests run without SDL2 or graphics
- Resource tests optional (gracefully skip if files missing)
- Fast: entire suite runs in < 1 second

## License

Tests are part of Johnny Reborn and use the same GPLv3 license.
