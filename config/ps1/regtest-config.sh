#!/bin/bash
# Regtest configuration — shared by all regtest scripts.
# Source this file; do not execute directly.

# DuckStation regtest binary
REGTEST_BIN="${REGTEST_BIN:-duckstation-regtest}"

# Docker image used for PS1 builds
REGTEST_BUILD_IMAGE="${REGTEST_BUILD_IMAGE:-jc-reborn-ps1-dev:amd64}"

# Number of emulated frames to run.
# BIOS boot takes ~15 sec, title ~10 sec, ocean transition ~10 sec.
# Scene content appears at ~frame 3300 (55 sec).
# 9000 frames (150 sec) gives headroom for scene playback + animation.
REGTEST_FRAMES="${REGTEST_FRAMES:-9000}"

# Frame capture interval (capture one frame every N frames)
REGTEST_INTERVAL="${REGTEST_INTERVAL:-60}"

# Maximum concurrent scene tests
REGTEST_PARALLEL="${REGTEST_PARALLEL:-4}"

# DuckStation log level for headless regtest runs.
REGTEST_LOG_LEVEL="${REGTEST_LOG_LEVEL:-Info}"

# Force deterministic scene playback in paired headless runs unless the caller
# explicitly chooses another seed.
REGTEST_SEED="${REGTEST_SEED:-1}"

# Per-scene timeout in seconds (wall-clock; kills runaway tests)
# 9000 frames at ~470 FPS headless = ~19 sec; allow 60 sec for safety.
REGTEST_TIMEOUT="${REGTEST_TIMEOUT:-60}"

# Default output root for results
REGTEST_OUTPUT_DIR="${REGTEST_OUTPUT_DIR:-regtest-results}"

# Scene list
REGTEST_SCENE_LIST="${REGTEST_SCENE_LIST:-config/ps1/regtest-scenes.txt}"

# Project root — scripts source this file after cd-ing to project root,
# so PWD is already correct.
REGTEST_PROJECT_ROOT="${REGTEST_PROJECT_ROOT:-$PWD}"
