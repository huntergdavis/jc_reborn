cmake_minimum_required(VERSION 3.21)

# Find PSn00bSDK first (required before project())
if(NOT DEFINED ENV{PSN00BSDK})
    message(FATAL_ERROR "PSN00BSDK environment variable not set")
endif()

# Set toolchain file BEFORE project()
list(APPEND CMAKE_MODULE_PATH "$ENV{PSN00BSDK}/lib/libpsn00b/cmake")
include(sdk)

# Project configuration
project(
    JohnnyReborn
    LANGUAGES C
    VERSION 1.0.0
)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags matching the original project
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wpedantic -DPS1_BUILD -ffreestanding")

# Source files - using PS1-specific implementations for graphics/sound/events
set(SOURCES
    jc_reborn.c
    utils.c
    uncompress.c
    resource.c
    dump.c
    story.c
    walk.c
    calcpath.c
    ads.c
    ttm.c
    island.c
    bench.c
    config.c
    # PS1-specific implementations
    graphics_ps1.c
    sound_ps1.c
    events_ps1.c
    cdrom_ps1.c
    ps1_debug.c
    pause_menu.c
    ps1_features.c
    ps1_captions.c
    ps1_accessibility.c
    # LibC stubs for missing functions
    ps1_stubs.c
)

# Create PS1 executable
# Note: GPREL = use GP-relative addressing for faster data access
psn00bsdk_add_executable(jcreborn GPREL ${SOURCES})

# Include directories
target_include_directories(jcreborn
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link PSn00bSDK libraries explicitly
target_link_libraries(jcreborn PRIVATE
    psxgpu
    psxgte
    psxspu
    psxcd
    c
)

# CD image creation is now handled by make-cd-image.sh script
# (POST_BUILD mkpsxiso was causing hangs when trying to overwrite existing files)

# Install target
install(TARGETS jcreborn)
