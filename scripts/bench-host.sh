#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RES_DIR="$PROJECT_ROOT/jc_resources"
BIN="$PROJECT_ROOT/build-host/jc_reborn"

"$SCRIPT_DIR/build-host.sh"

if [ ! -f "$RES_DIR/RESOURCE.MAP" ] || [ ! -f "$RES_DIR/RESOURCE.001" ]; then
    cat >&2 <<EOF
ERROR: Missing Johnny Castaway resource files in $RES_DIR

Required:
- RESOURCE.MAP
- RESOURCE.001
EOF
    exit 2
fi

pushd "$RES_DIR" >/dev/null
"$BIN" window bench "$@"
popd >/dev/null
