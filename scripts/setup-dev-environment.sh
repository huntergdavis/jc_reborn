#!/bin/bash
# setup-dev-environment.sh — Install everything needed for jc_reborn development
#
# Targets KDE Neon / Ubuntu / Pop!_OS / Debian-based systems.
# Installs: system packages, Docker, Python venv, SDL2, DuckStation flatpak,
#           PS1 Docker build image, regtest Docker image.
#
# Usage:
#   ./scripts/setup-dev-environment.sh          # full setup
#   ./scripts/setup-dev-environment.sh --check  # just verify what's installed
#
# This script uses sudo for apt-get only. All Docker/build commands run as user.

set -euo pipefail

if [ "$(id -u)" = "0" ]; then
    echo "ERROR: Do not run as root. The script will use sudo where needed." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

CHECK_ONLY=0
if [ "${1:-}" = "--check" ]; then
    CHECK_ONLY=1
fi

ok()   { echo "  [OK]  $1"; }
fail() { echo "  [--]  $1"; }
info() { echo "  ....  $1"; }

section() {
    echo ""
    echo "=== $1 ==="
}

# ---------------------------------------------------------------------------
section "System packages (apt)"
# ---------------------------------------------------------------------------

APT_PACKAGES=(
    # Build essentials
    build-essential cmake pkg-config git curl wget
    # SDL2 for host build
    libsdl2-dev
    # Python
    python3 python3-venv python3-pip python3-pil
    # Image tools (used by capture/check scripts)
    imagemagick
    # Flatpak (for DuckStation)
    flatpak
    # Docker prerequisites
    ca-certificates gnupg lsb-release
    # Misc tools used by scripts
    bc jq flock xdotool
)

missing_apt=()
for pkg in "${APT_PACKAGES[@]}"; do
    if dpkg -s "$pkg" &>/dev/null; then
        ok "$pkg"
    else
        fail "$pkg (not installed)"
        missing_apt+=("$pkg")
    fi
done

if [ "$CHECK_ONLY" -eq 0 ] && [ ${#missing_apt[@]} -gt 0 ]; then
    info "Installing ${#missing_apt[@]} missing packages..."
    sudo apt-get update -qq
    sudo apt-get install -y "${missing_apt[@]}"
    ok "System packages installed"
fi

# ---------------------------------------------------------------------------
section "Docker"
# ---------------------------------------------------------------------------

if command -v docker &>/dev/null; then
    ok "Docker $(docker --version 2>/dev/null | head -c 40)"
else
    fail "Docker not installed"
    if [ "$CHECK_ONLY" -eq 0 ]; then
        info "Installing Docker..."
        "$SCRIPT_DIR/setup-docker.sh"
    fi
fi

if groups "$USER" | grep -q docker 2>/dev/null; then
    ok "User in docker group"
else
    fail "User NOT in docker group"
    if [ "$CHECK_ONLY" -eq 0 ]; then
        sudo usermod -aG docker "$USER"
        echo "  !!  You must log out and back in for docker group to take effect."
    fi
fi

# ---------------------------------------------------------------------------
section "Docker images"
# ---------------------------------------------------------------------------

if docker image inspect jc-reborn-ps1-dev:amd64 &>/dev/null; then
    ok "jc-reborn-ps1-dev:amd64 (PS1 build image)"
else
    fail "jc-reborn-ps1-dev:amd64 not built"
    if [ "$CHECK_ONLY" -eq 0 ]; then
        info "Building PS1 dev image (first time takes ~5 min)..."
        "$SCRIPT_DIR/build-docker-image.sh"
    fi
fi

if docker image inspect jc-reborn-regtest:latest &>/dev/null; then
    ok "jc-reborn-regtest:latest (headless DuckStation)"
else
    fail "jc-reborn-regtest:latest not built"
    if [ "$CHECK_ONLY" -eq 0 ]; then
        info "Building regtest image (first time takes ~20 min)..."
        if [ -f "$SCRIPT_DIR/build-regtest-image.sh" ]; then
            "$SCRIPT_DIR/build-regtest-image.sh"
        else
            docker build --platform linux/amd64 \
                -f "$PROJECT_ROOT/config/ps1/Dockerfile.regtest" \
                -t jc-reborn-regtest:latest "$PROJECT_ROOT"
        fi
    fi
fi

# ---------------------------------------------------------------------------
section "DuckStation (live testing)"
# ---------------------------------------------------------------------------

if flatpak info org.duckstation.DuckStation &>/dev/null; then
    ok "DuckStation flatpak installed"
else
    fail "DuckStation flatpak not installed"
    if [ "$CHECK_ONLY" -eq 0 ]; then
        info "Installing DuckStation via flatpak..."
        flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
        flatpak install -y flathub org.duckstation.DuckStation
    fi
fi

# PS1 BIOS check
BIOS_DIRS=(
    "$HOME/.var/app/org.duckstation.DuckStation/config/duckstation/bios"
    "$HOME/.local/share/duckstation/bios"
    "$HOME/.config/duckstation/bios"
)
bios_found=0
for d in "${BIOS_DIRS[@]}"; do
    if ls "$d"/*.bin &>/dev/null 2>&1; then
        ok "PS1 BIOS found in $d"
        bios_found=1
        break
    fi
done
if [ "$bios_found" -eq 0 ]; then
    fail "PS1 BIOS not found"
    echo "  !!  Place scph1001.bin (or similar) in one of:"
    for d in "${BIOS_DIRS[@]}"; do
        echo "        $d"
    done
fi

# ---------------------------------------------------------------------------
section "Python environment"
# ---------------------------------------------------------------------------

if [ -d "$PROJECT_ROOT/.venv-vlm" ]; then
    ok "Python venv exists (.venv-vlm/)"
else
    fail "Python venv not found"
    if [ "$CHECK_ONLY" -eq 0 ]; then
        info "Creating Python virtual environment..."
        python3 -m venv "$PROJECT_ROOT/.venv-vlm"
        "$PROJECT_ROOT/.venv-vlm/bin/pip" install --upgrade pip
        "$PROJECT_ROOT/.venv-vlm/bin/pip" install pillow numpy
        ok "Python venv created with pillow, numpy"
    fi
fi

# ---------------------------------------------------------------------------
section "Host build"
# ---------------------------------------------------------------------------

if [ -f "$PROJECT_ROOT/jc_reborn-host" ]; then
    ok "Host binary exists (jc_reborn-host)"
else
    fail "Host binary not built"
    if [ "$CHECK_ONLY" -eq 0 ] && [ -f "$PROJECT_ROOT/Makefile.linux" ]; then
        info "Building host binary..."
        make -C "$PROJECT_ROOT" -f Makefile.linux -j"$(nproc)" 2>/dev/null && ok "Host built" || fail "Host build failed"
    fi
fi

# ---------------------------------------------------------------------------
section "PS1 build"
# ---------------------------------------------------------------------------

if [ -f "$PROJECT_ROOT/build-ps1/jcreborn.exe" ]; then
    ok "PS1 executable exists (build-ps1/jcreborn.exe)"
else
    fail "PS1 executable not built"
    if [ "$CHECK_ONLY" -eq 0 ]; then
        info "Building PS1 executable..."
        "$SCRIPT_DIR/build-ps1.sh" && ok "PS1 built" || fail "PS1 build failed"
    fi
fi

# ---------------------------------------------------------------------------
section "Game resources"
# ---------------------------------------------------------------------------

if [ -f "$PROJECT_ROOT/jc_resources/RESOURCE.MAP" ]; then
    ok "RESOURCE.MAP found"
else
    fail "RESOURCE.MAP not found"
    echo "  !!  Place original Johnny Castaway resource files in jc_resources/"
fi

if ls "$PROJECT_ROOT/jc_resources/transcoded/"*.PSB &>/dev/null 2>&1; then
    count=$(ls "$PROJECT_ROOT/jc_resources/transcoded/"*.PSB 2>/dev/null | wc -l)
    ok "Transcoded PSB files: $count"
else
    fail "No transcoded PSB files found"
    echo "  !!  Run the BMP transcoder to generate PS1-format sprite packs"
fi

# ---------------------------------------------------------------------------
section "Summary"
# ---------------------------------------------------------------------------

echo ""
echo "Quick-start commands:"
echo "  ./scripts/build-ps1.sh                    # Build PS1 executable"
echo "  ./scripts/make-cd-image.sh                # Create CD image"
echo "  ./scripts/rebuild-and-let-run.sh noclean  # Build + launch in DuckStation"
echo "  ./scripts/regtest-scene.sh --scene 'STAND 2'  # Headless scene test"
echo ""
