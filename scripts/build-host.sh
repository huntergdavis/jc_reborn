#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-host"
BIN="$BUILD_DIR/jc_reborn"
COMPAT_LINK="$PROJECT_ROOT/jc_reborn-host"

CC="${CC:-cc}"
CFLAGS="${CFLAGS:-}"
LDFLAGS="${LDFLAGS:-}"

detect_sdl() {
    if command -v sdl2-config >/dev/null 2>&1; then
        SDL_CFLAGS="$(sdl2-config --cflags | sed 's|/include/SDL2|/include|g')"
        SDL_LIBS="$(sdl2-config --libs)"
        return 0
    fi

    if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl2; then
        SDL_CFLAGS="$(pkg-config --cflags sdl2)"
        SDL_LIBS="$(pkg-config --libs sdl2)"
        return 0
    fi

    cat >&2 <<'EOF'
ERROR: Could not find SDL2 build flags.

Supported detection methods:
- sdl2-config
- pkg-config sdl2

Install SDL2 development headers/tools first, then retry.
Examples:
- Ubuntu/Debian: sudo apt-get install libsdl2-dev pkg-config
- Fedora: sudo dnf install SDL2-devel pkgconf-pkg-config
- macOS: brew install sdl2
EOF
    exit 2
}

detect_sdl

mkdir -p "$BUILD_DIR"

SOURCES=(
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
    graphics.c
    sound.c
    events.c
    config.c
)

pushd "$PROJECT_ROOT" >/dev/null
"$CC" \
    -Wall -Wpedantic -std=c99 -O2 \
    $SDL_CFLAGS \
    $CFLAGS \
    "${SOURCES[@]}" \
    -o "$BIN" \
    $SDL_LIBS \
    $LDFLAGS
popd >/dev/null

echo "Built host binary: $BIN"
ln -sfn "build-host/jc_reborn" "$COMPAT_LINK"
echo "Updated compatibility link: $COMPAT_LINK"
