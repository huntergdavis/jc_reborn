#!/bin/bash
# Shared Docker command detection for non-root repo scripts.

set -euo pipefail

docker_init() {
    if [ "$(id -u)" = "0" ]; then
        echo "ERROR: Do not run this script as root/sudo." >&2
        exit 1
    fi

    if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
        DOCKER_CMD=(docker)
        return 0
    fi

    if command -v sudo >/dev/null 2>&1 && sudo -n docker info >/dev/null 2>&1; then
        DOCKER_CMD=(sudo docker)
        return 0
    fi

    echo "ERROR: Docker is installed but not usable from this shell." >&2
    echo "       Either add this shell to the docker group or allow passwordless sudo for docker." >&2
    exit 1
}

docker_maybe_init() {
    if [ "$(id -u)" = "0" ]; then
        echo "ERROR: Do not run this script as root/sudo." >&2
        exit 1
    fi

    if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
        DOCKER_CMD=(docker)
        return 0
    fi

    if command -v sudo >/dev/null 2>&1 && sudo -n docker info >/dev/null 2>&1; then
        DOCKER_CMD=(sudo docker)
        return 0
    fi

    return 1
}
