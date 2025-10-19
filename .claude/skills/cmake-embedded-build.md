# CMake Embedded Build System Skill

Set up CMake build systems for embedded platforms with cross-compilation toolchains.

## Usage

Run this skill when creating or fixing CMake configurations for PS1, Dreamcast, or other embedded targets.

## Task

### 1. Understand Toolchain Requirements

Research the target SDK:
- CMake module location (e.g., `$SDK/lib/cmake/sdk.cmake`)
- Required CMake functions (e.g., `psn00bsdk_add_executable()`)
- Compiler flags needed (`-ffreestanding`, `-G0`, etc.)
- Linker requirements (libraries, startup files)
- Special executable types (STATIC, GPREL, DYNAMIC)

### 2. Create CMakeLists.txt Structure

```cmake
cmake_minimum_required(VERSION 3.21)

# Include toolchain BEFORE project()
if(NOT DEFINED ENV{SDK_PATH})
    message(FATAL_ERROR "SDK_PATH environment variable not set")
endif()

list(APPEND CMAKE_MODULE_PATH "$ENV{SDK_PATH}/lib/cmake")
include(sdk)

# Project definition
project(
    ProjectName
    LANGUAGES C
    VERSION 1.0.0
)

# C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Platform-specific compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wpedantic")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DPLATFORM_BUILD -ffreestanding")

# Source files
set(SOURCES
    main.c
    core_file1.c
    core_file2.c
    platform_specific1.c
    platform_specific2.c
)

# Create executable (platform-specific function)
platform_add_executable(projectname GPREL ${SOURCES})

# Include directories
target_include_directories(projectname
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Optional: CD image generation
# add_custom_command(
#     TARGET projectname POST_BUILD
#     COMMAND mkpsxiso cd_layout.xml
#     WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
# )
```

### 3. Common Platform Patterns

#### PlayStation 1 (PSn00bSDK)
```cmake
# Include PSn00bSDK
list(APPEND CMAKE_MODULE_PATH "$ENV{PSN00BSDK}/lib/libpsn00b/cmake")
include(sdk)

# Executable types: STATIC, GPREL, DYNAMIC, NOGPREL
psn00bsdk_add_executable(target GPREL ${SOURCES})

# Flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DPS1_BUILD -ffreestanding")
```

#### Dreamcast (KallistiOS)
```cmake
# Set KOS paths
set(ENV{KOS_BASE} "/opt/toolchains/dc/kos")
include($ENV{KOS_BASE}/utils/cmake/kos.cmake)

# Create executable
add_executable(target ${SOURCES})
target_link_libraries(target ${KOS_LIBS})
```

#### Generic Embedded
```cmake
# Set toolchain file
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/cmake/toolchain.cmake")

# Cross-compiler settings
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
```

### 4. Debug Configuration Issues

Common CMake errors and fixes:

**Error: "Invalid executable type"**
```cmake
# Missing type parameter
psn00bsdk_add_executable(target ${SOURCES})  # WRONG
psn00bsdk_add_executable(target GPREL ${SOURCES})  # CORRECT
```

**Error: "SDK not found"**
```cmake
# Check environment variable
if(NOT DEFINED ENV{PSN00BSDK})
    message(FATAL_ERROR "PSN00BSDK not set: export PSN00BSDK=/path/to/sdk")
endif()
```

**Error: "Toolchain not found"**
```cmake
# Toolchain must be included BEFORE project()
include(sdk)  # Must be BEFORE project()
project(MyProject LANGUAGES C)
```

### 5. Test Build

```bash
# Create build directory
mkdir -p build
cd build

# Configure
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build
make

# Verbose build (for debugging)
make VERBOSE=1
```

### 6. Docker Integration

For Docker-based builds:
```bash
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  sdk-dev:amd64 \
  bash -c "cd /project && \
    mkdir -p build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make"
```

## Troubleshooting Checklist

- [ ] SDK environment variable set correctly
- [ ] Toolchain included BEFORE project()
- [ ] Executable type specified correctly
- [ ] Compiler flags include platform define (-DPS1_BUILD)
- [ ] All source files listed in SOURCES
- [ ] Include directories configured
- [ ] Clean build directory (rm -rf build)

## Platform-Specific Flags Reference

### PS1
- `-ffreestanding` - No standard library
- `-G0` - Disable GP-relative addressing (if not using GPREL)
- `-msoft-float` - No hardware FPU
- `-O2` - Optimization level

### Dreamcast
- `-ml` - Little endian
- `-m4-single-only` - Single precision FPU
- `-ffunction-sections` - Dead code elimination

### ARM Embedded
- `-mcpu=cortex-m4` - CPU selection
- `-mthumb` - Thumb instruction set
- `-mfloat-abi=hard` - Hardware FPU ABI

## Success Criteria

- CMake configures without errors
- All source files compile
- Executable links successfully
- Build artifacts in expected locations
- Build time reasonable (<2 min for small projects)
