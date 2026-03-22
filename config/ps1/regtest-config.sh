#!/bin/bash
# Regtest configuration — shared by all regtest scripts.
# Source this file; do not execute directly.

# DuckStation regtest binary
REGTEST_BIN="${REGTEST_BIN:-duckstation-regtest}"

# Docker image used for PS1 builds
REGTEST_BUILD_IMAGE="${REGTEST_BUILD_IMAGE:-jc-reborn-ps1-dev:amd64}"

# Number of emulated frames to run (60 fps => 1800 = 30 seconds)
REGTEST_FRAMES="${REGTEST_FRAMES:-1800}"

# Frame capture interval (capture one frame every N frames)
REGTEST_INTERVAL="${REGTEST_INTERVAL:-60}"

# Maximum concurrent scene tests
REGTEST_PARALLEL="${REGTEST_PARALLEL:-4}"

# Per-scene timeout in seconds (wall-clock; kills runaway tests)
REGTEST_TIMEOUT="${REGTEST_TIMEOUT:-120}"

# Default output root for results
REGTEST_OUTPUT_DIR="${REGTEST_OUTPUT_DIR:-regtest-results}"

# Scene list
REGTEST_SCENE_LIST="${REGTEST_SCENE_LIST:-config/ps1/regtest-scenes.txt}"

# Project root — scripts source this file after cd-ing to project root,
# so PWD is already correct.
REGTEST_PROJECT_ROOT="${REGTEST_PROJECT_ROOT:-$PWD}"
