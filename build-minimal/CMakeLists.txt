cmake_minimum_required(VERSION 3.21)

# Find PSn00bSDK first
if(NOT DEFINED ENV{PSN00BSDK})
    message(FATAL_ERROR "PSN00BSDK environment variable not set")
endif()

# Set toolchain file BEFORE project()
list(APPEND CMAKE_MODULE_PATH "$ENV{PSN00BSDK}/lib/libpsn00b/cmake")
include(sdk)

# Project configuration
project(
    PS1Minimal
    LANGUAGES C
    VERSION 1.0.0
)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DPS1_BUILD -ffreestanding")

# Create PS1 executable from minimal test
# psn00bsdk_add_executable creates the executable and converts to PS1 format
psn00bsdk_add_executable(ps1_minimal GPREL ps1_minimal_main.c)
