#!/bin/bash
# build-binary-library.sh — Build a PS1 executable + CD image for every
#                           PS1 code/tooling-changing commit since the port began.
#
# Creates a directory of numbered, date-stamped builds that can be tested
# against any scene to find exact regressions.
#
# Usage:
#   ./scripts/build-binary-library.sh                    # build all
#   ./scripts/build-binary-library.sh --resume           # skip already-built
#   ./scripts/build-binary-library.sh --dry-run          # list commits only
#   ./scripts/build-binary-library.sh --output ~/binlib  # custom output dir
#   ./scripts/build-binary-library.sh --smoke-test       # add quick regtest
#
# Requires: Docker (jc-reborn-ps1-dev:amd64 image), git, jc_resources/

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
# shellcheck source=./docker-common.sh
source "$PROJECT_ROOT/scripts/docker-common.sh"
docker_init

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------
PS1_PORT_ORIGIN="a8c0599b"   # First PS1 port commit
END_REF="HEAD"
OUTPUT_DIR="$PROJECT_ROOT/binary-library"
DRY_RUN=0
RESUME=0
SMOKE_TEST=0
DOCKER_IMAGE="jc-reborn-ps1-dev:amd64"

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
while [ $# -gt 0 ]; do
    case "$1" in
        --start)     PS1_PORT_ORIGIN="$2"; shift 2 ;;
        --end)       END_REF="$2"; shift 2 ;;
        --output)    OUTPUT_DIR="$2"; shift 2 ;;
        --dry-run)   DRY_RUN=1; shift ;;
        --resume)    RESUME=1; shift ;;
        --smoke-test) SMOKE_TEST=1; shift ;;
        -h|--help)
            sed -n '2,12p' "$0" | sed 's/^# *//'
            exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

# ---------------------------------------------------------------------------
# Validation
# ---------------------------------------------------------------------------
cd "$PROJECT_ROOT"

if ! "${DOCKER_CMD[@]}" image inspect "$DOCKER_IMAGE" >/dev/null 2>&1; then
    echo "ERROR: Docker image '$DOCKER_IMAGE' not found." >&2
    echo "Run: ./scripts/build-docker-image.sh" >&2
    exit 1
fi

# Locate jc_resources (needed for CD images)
JC_RESOURCES=""
for candidate in "$PROJECT_ROOT/jc_resources" "$HOME/jc_resources"; do
    if [ -f "$candidate/RESOURCE.MAP" ]; then
        JC_RESOURCES="$(realpath "$candidate")"
        break
    fi
done
if [ -z "$JC_RESOURCES" ]; then
    echo "ERROR: jc_resources/RESOURCE.MAP not found." >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Enumerate commits
# ---------------------------------------------------------------------------
echo "Enumerating commits from $PS1_PORT_ORIGIN to $END_REF..."

mapfile -t COMMITS < <(
    git rev-list --reverse "$PS1_PORT_ORIGIN"^.."$END_REF" -- \
        '*.c' '*.h' \
        'scripts/*.sh' 'scripts/*.py' \
        'config/ps1/*.sh'
)
TOTAL=${#COMMITS[@]}

if [ "$TOTAL" -eq 0 ]; then
    echo "No code-changing commits found in range."
    exit 0
fi

echo "Found $TOTAL code-changing commits."

if [ "$DRY_RUN" -eq 1 ]; then
    echo ""
    SEQ=0
    for COMMIT in "${COMMITS[@]}"; do
        SEQ=$((SEQ + 1))
        DATE=$(git log --format="%ai" -1 "$COMMIT" | sed 's/ /_/g;s/[:-]//g' | cut -c1-15)
        SHORT=$(git rev-parse --short=8 "$COMMIT")
        MSG=$(git log --format="%s" -1 "$COMMIT" | cut -c1-60)
        printf "%03d  %s  %s  %s\n" "$SEQ" "$SHORT" "$DATE" "$MSG"
    done
    echo ""
    echo "Total: $TOTAL commits (dry run, nothing built)"
    exit 0
fi

# ---------------------------------------------------------------------------
# Setup worktree (sparse — only source files, not 12GB of regtest BMPs)
# ---------------------------------------------------------------------------
TMP_ROOT="${TMPDIR:-/tmp}"
WORKTREE="$TMP_ROOT/jc_reborn_binlib_$$"
cleanup_worktree() {
    echo "Cleaning up worktree..."
    # Docker creates root-owned files in build-ps1/; fix ownership first
    "${DOCKER_CMD[@]}" run --rm --platform linux/amd64 -v "$WORKTREE":/project "$DOCKER_IMAGE" \
        bash -c "chown -R $(id -u):$(id -g) /project/build-ps1 2>/dev/null" 2>/dev/null || true
    git -C "$PROJECT_ROOT" worktree remove --force "$WORKTREE" 2>/dev/null || rm -rf "$WORKTREE"
    echo "Done."
}
trap cleanup_worktree EXIT

echo "Creating sparse worktree at $WORKTREE..."
git worktree add --detach --no-checkout "$WORKTREE" "$PS1_PORT_ORIGIN"
# Use non-cone sparse checkout to include root-level source files
git -C "$WORKTREE" sparse-checkout init --no-cone
git -C "$WORKTREE" sparse-checkout set \
    '/*.c' '/*.h' \
    '/CMake*' '/Makefile*' \
    '/config/ps1/' \
    '/generated/ps1/' \
    '/scripts/'
git -C "$WORKTREE" checkout

mkdir -p "$OUTPUT_DIR"

next_sequence_base() {
    local outdir="$1"
    python3 - "$outdir" <<'PY'
import json
import os
import sys

outdir = sys.argv[1]
max_seq = 0
if os.path.isdir(outdir):
    for name in os.listdir(outdir):
        meta_path = os.path.join(outdir, name, "metadata.json")
        if not os.path.isfile(meta_path):
            continue
        try:
            with open(meta_path, encoding="utf-8") as f:
                seq = int(json.load(f).get("sequence", 0))
        except Exception:
            continue
        max_seq = max(max_seq, seq)
print(max_seq)
PY
}

# ---------------------------------------------------------------------------
# Counters
# ---------------------------------------------------------------------------
NUM_SUCCESS=0
NUM_FAILED=0
NUM_SKIPPED=0
NUM_RESUMED=0
TIME_START=$(date +%s)

# ---------------------------------------------------------------------------
# Build loop
# ---------------------------------------------------------------------------
SEQ="$(next_sequence_base "$OUTPUT_DIR")"
for COMMIT in "${COMMITS[@]}"; do
    SEQ=$((SEQ + 1))
    SHORT=$(git rev-parse --short=8 "$COMMIT")
    COMMIT_DATE_RAW=$(git log --format="%ai" -1 "$COMMIT")
    COMMIT_DATE=$(echo "$COMMIT_DATE_RAW" | sed 's/ /_/;s/://g;s/ .*//' | tr -d '-')
    COMMIT_DATE_ISO=$(git log --format="%aI" -1 "$COMMIT")
    COMMIT_MSG=$(git log --format="%s" -1 "$COMMIT")
    COMMIT_AUTHOR=$(git log --format="%an" -1 "$COMMIT")
    DIR_NAME="$(printf '%03d' "$SEQ")_${COMMIT_DATE}_${SHORT}"
    OUTDIR="$OUTPUT_DIR/$DIR_NAME"

    # Resume support
    if [ "$RESUME" -eq 1 ] && [ -f "$OUTDIR/metadata.json" ]; then
        NUM_RESUMED=$((NUM_RESUMED + 1))
        printf "[%3d/%d] %s RESUMED (already built)\n" "$SEQ" "$TOTAL" "$SHORT"
        continue
    fi

    printf "[%3d/%d] %s %s ... " "$SEQ" "$TOTAL" "$SHORT" "$(echo "$COMMIT_MSG" | cut -c1-45)"

    BUILD_START=$(date +%s)
    mkdir -p "$OUTDIR"
    BUILD_LOG="$OUTDIR/build.log"
    BUILD_STATUS="failed"
    BUILD_EXIT=1
    EXE_EXISTS=false
    ISO_EXISTS=false
    EXE_SIZE=0
    ISO_SIZE=0
    EXE_SHA=""
    ISO_SHA=""
    CD_LAYOUT_USED=""

    # Step 1: Checkout
    git -C "$WORKTREE" checkout --force "$COMMIT" >> "$BUILD_LOG" 2>&1 || true
    git -C "$WORKTREE" clean -fdx >> "$BUILD_LOG" 2>&1 || true

    # Step 2: Neutral bootmode
    mkdir -p "$WORKTREE/config/ps1"
    : > "$WORKTREE/config/ps1/BOOTMODE.TXT"
    cat > "$WORKTREE/config/ps1/bootmode_embedded.h" << 'HEADER'
#ifndef PS1_BOOTMODE_EMBEDDED_H
#define PS1_BOOTMODE_EMBEDDED_H
#define PS1_EMBEDDED_BOOT_OVERRIDE ""
#endif
HEADER
    # Also write to root for early-era commits
    : > "$WORKTREE/BOOTMODE.TXT" 2>/dev/null || true

    # Step 3: Ensure resources are accessible inside Docker
    # Docker can't follow symlinks outside the mount, so we bind-mount
    # jc_resources separately in the Docker run commands below.
    # Copy TITLE.RAW and root-level resource files directly.
    for f in RESOURCE.MAP RESOURCE.001; do
        [ ! -f "$WORKTREE/$f" ] && [ -f "$JC_RESOURCES/$f" ] && cp "$JC_RESOURCES/$f" "$WORKTREE/$f" 2>/dev/null || true
    done
    [ ! -f "$WORKTREE/TITLE.RAW" ] && [ -f "$PROJECT_ROOT/TITLE.RAW" ] && cp "$PROJECT_ROOT/TITLE.RAW" "$WORKTREE/TITLE.RAW" 2>/dev/null || true
    # Remove any stale jc_resources symlink (Docker can't follow it)
    rm -f "$WORKTREE/jc_resources" 2>/dev/null || true

    # Docker bind-mounts: worktree as /project, jc_resources overlay
    DOCKER_MOUNTS=(-v "$WORKTREE":/project -v "$JC_RESOURCES":/project/jc_resources:ro)

    # Step 4: CMake configure (always needed — Makefile isn't tracked in git)
    if [ -f "$WORKTREE/CMakeLists.ps1.txt" ]; then
        "${DOCKER_CMD[@]}" run --rm --platform linux/amd64 \
            "${DOCKER_MOUNTS[@]}" \
            "$DOCKER_IMAGE" \
            bash -c 'mkdir -p /project/build-ps1 && cd /project/build-ps1 && cmake -DCMAKE_BUILD_TYPE=Release .. 2>&1' \
            >> "$BUILD_LOG" 2>&1 || true
    else
        echo "No CMakeLists.ps1.txt found" >> "$BUILD_LOG"
        BUILD_STATUS="skipped_no_buildsystem"
        NUM_SKIPPED=$((NUM_SKIPPED + 1))
        python3 -c "
import json
json.dump({
    'sequence': $SEQ,
    'dir_name': '$DIR_NAME',
    'commit': {
        'hash': '$COMMIT',
        'short_hash': '$SHORT',
        'date': '$COMMIT_DATE_ISO',
        'author': $(python3 -c "import json; print(json.dumps('$COMMIT_AUTHOR'))"),
        'message': $(python3 -c "import json,sys; print(json.dumps(sys.argv[1]))" "$COMMIT_MSG")
    },
    'build': {
        'status': '$BUILD_STATUS',
        'exe_exists': False,
        'iso_exists': False
    }
}, open('$OUTDIR/metadata.json', 'w'), indent=2)
"
        echo "SKIP (no build system)"
        continue
    fi

    # Step 5: Build PS1 executable
    BUILD_EXIT=0
    "${DOCKER_CMD[@]}" run --rm --platform linux/amd64 \
        "${DOCKER_MOUNTS[@]}" \
        "$DOCKER_IMAGE" \
        bash -c 'cd /project/build-ps1 && make -j4 jcreborn 2>&1' \
        >> "$BUILD_LOG" 2>&1 || BUILD_EXIT=$?

    if [ "$BUILD_EXIT" -eq 0 ] && [ -f "$WORKTREE/build-ps1/jcreborn.exe" ]; then
        EXE_EXISTS=true
        EXE_SIZE=$(stat -c%s "$WORKTREE/build-ps1/jcreborn.exe" 2>/dev/null || echo 0)
        EXE_SHA=$(sha256sum "$WORKTREE/build-ps1/jcreborn.exe" 2>/dev/null | cut -d' ' -f1 || echo "")
    else
        BUILD_STATUS="failed"
        NUM_FAILED=$((NUM_FAILED + 1))
    fi

    # Step 6: Create CD image (only if EXE built)
    if [ "$EXE_EXISTS" = true ]; then
        CD_LAYOUT=""
        if [ -f "$WORKTREE/config/ps1/cd_layout.xml" ]; then
            CD_LAYOUT="/project/config/ps1/cd_layout.xml"
            CD_LAYOUT_USED="config/ps1/cd_layout.xml"
        elif [ -f "$WORKTREE/cd_layout.xml" ]; then
            CD_LAYOUT="/project/cd_layout.xml"
            CD_LAYOUT_USED="cd_layout.xml"
        fi

        if [ -n "$CD_LAYOUT" ]; then
            rm -f "$WORKTREE/jcreborn.bin" "$WORKTREE/jcreborn.cue"
            "${DOCKER_CMD[@]}" run --rm --platform linux/amd64 \
                "${DOCKER_MOUNTS[@]}" \
                "$DOCKER_IMAGE" \
                bash -c "cd /project && mkpsxiso -y $CD_LAYOUT 2>&1" \
                >> "$BUILD_LOG" 2>&1 || true

            if [ -f "$WORKTREE/jcreborn.bin" ]; then
                ISO_EXISTS=true
                ISO_SIZE=$(stat -c%s "$WORKTREE/jcreborn.bin" 2>/dev/null || echo 0)
                ISO_SHA=$(sha256sum "$WORKTREE/jcreborn.bin" 2>/dev/null | cut -d' ' -f1 || echo "")
                BUILD_STATUS="success"
                NUM_SUCCESS=$((NUM_SUCCESS + 1))
            elif [ "$BUILD_STATUS" != "failed" ]; then
                BUILD_STATUS="exe_only"
                NUM_SUCCESS=$((NUM_SUCCESS + 1))
            fi
        else
            BUILD_STATUS="exe_only"
            CD_LAYOUT_USED="none"
            NUM_SUCCESS=$((NUM_SUCCESS + 1))
        fi
    fi

    BUILD_END=$(date +%s)
    BUILD_DURATION=$((BUILD_END - BUILD_START))

    # Step 7: Copy artifacts
    if [ "$EXE_EXISTS" = true ]; then
        cp "$WORKTREE/build-ps1/jcreborn.exe" "$OUTDIR/jcreborn.exe"
    fi
    if [ "$ISO_EXISTS" = true ]; then
        cp "$WORKTREE/jcreborn.bin" "$OUTDIR/jcreborn.bin"
        cp "$WORKTREE/jcreborn.cue" "$OUTDIR/jcreborn.cue"
    fi

    # Step 8: Metadata
    python3 -c "
import json, sys
json.dump({
    'sequence': $SEQ,
    'dir_name': '$DIR_NAME',
    'commit': {
        'hash': '$COMMIT',
        'short_hash': '$SHORT',
        'date': '$COMMIT_DATE_ISO',
        'author': $(python3 -c "import json; print(json.dumps('$COMMIT_AUTHOR'))"),
        'message': $(python3 -c "import json,sys; print(json.dumps(sys.argv[1]))" "$COMMIT_MSG")
    },
    'build': {
        'status': '$BUILD_STATUS',
        'exit_code': $BUILD_EXIT,
        'duration_seconds': $BUILD_DURATION,
        'exe_exists': $( [ "$EXE_EXISTS" = true ] && echo "True" || echo "False" ),
        'exe_size_bytes': $EXE_SIZE,
        'exe_sha256': $(python3 -c "import json; print(json.dumps('$EXE_SHA'))"),
        'iso_exists': $( [ "$ISO_EXISTS" = true ] && echo "True" || echo "False" ),
        'iso_size_bytes': $ISO_SIZE,
        'iso_sha256': $(python3 -c "import json; print(json.dumps('$ISO_SHA'))"),
        'cd_layout_path': $(python3 -c "import json; print(json.dumps('$CD_LAYOUT_USED'))")
    }
}, open('$OUTDIR/metadata.json', 'w'), indent=2)
print()
"

    # Step 9: Clean build dir for next iteration (fresh cmake each time)
    "${DOCKER_CMD[@]}" run --rm --platform linux/amd64 -v "$WORKTREE":/project "$DOCKER_IMAGE" \
        bash -c "rm -rf /project/build-ps1" 2>/dev/null || true
    rm -f "$WORKTREE/jcreborn.bin" "$WORKTREE/jcreborn.cue"

    # Print result
    echo "$BUILD_STATUS (${BUILD_DURATION}s)"
done

# ---------------------------------------------------------------------------
# Optional smoke test pass
# ---------------------------------------------------------------------------
if [ "$SMOKE_TEST" -eq 1 ]; then
    echo ""
    echo "=== Smoke Test Pass ==="
    REGTEST_AVAILABLE=0
    if "${DOCKER_CMD[@]}" image inspect jc-reborn-regtest:latest >/dev/null 2>&1; then
        REGTEST_AVAILABLE=1
    elif command -v duckstation-regtest >/dev/null 2>&1; then
        REGTEST_AVAILABLE=1
    fi

    if [ "$REGTEST_AVAILABLE" -eq 0 ]; then
        echo "WARNING: No regtest binary or Docker image available. Skipping smoke tests."
    else
        for DIR in "$OUTPUT_DIR"/*/; do
            if [ ! -f "$DIR/jcreborn.cue" ]; then
                continue
            fi
            if [ -f "$DIR/smoke_result.json" ] && [ "$RESUME" -eq 1 ]; then
                continue
            fi
            DIRNAME=$(basename "$DIR")
            printf "  Smoke: %s ... " "$DIRNAME"
            SMOKE_EXIT=0
            "$SCRIPT_DIR/run-regtest.sh" \
                --cue "$DIR/jcreborn.cue" \
                --frames 300 \
                --dumpinterval 300 \
                --dumpdir "$DIR/smoke" \
                --timeout 30 \
                > "$DIR/smoke.log" 2>&1 || SMOKE_EXIT=$?

            SMOKE_FRAMES=$(find "$DIR/smoke" -name "frame_*.png" 2>/dev/null | wc -l)
            SMOKE_STATUS="fail"
            [ "$SMOKE_FRAMES" -gt 0 ] && SMOKE_STATUS="pass"
            echo '{"status":"'"$SMOKE_STATUS"'","frames":'"$SMOKE_FRAMES"',"exit_code":'"$SMOKE_EXIT"'}' > "$DIR/smoke_result.json"
            echo "$SMOKE_STATUS ($SMOKE_FRAMES frames)"
        done
    fi
fi

# ---------------------------------------------------------------------------
# Generate summary index
# ---------------------------------------------------------------------------
TIME_END=$(date +%s)
TOTAL_TIME=$((TIME_END - TIME_START))

echo ""
echo "=== Generating Index ==="

# JSON index
python3 - "$OUTPUT_DIR" << 'PYEOF'
import json, os, sys, csv

outdir = sys.argv[1]
entries = []

for name in sorted(os.listdir(outdir)):
    meta_path = os.path.join(outdir, name, "metadata.json")
    if os.path.isfile(meta_path):
        with open(meta_path) as f:
            entries.append(json.load(f))

entries.sort(key=lambda e: (int(e.get("sequence", 0)), e.get("dir_name", "")))

# Write JSON index
with open(os.path.join(outdir, "index.json"), "w") as f:
    json.dump(entries, f, indent=2)

# Write CSV index
with open(os.path.join(outdir, "index.csv"), "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["sequence", "short_hash", "date", "status", "exe_size", "iso_size", "message"])
    for e in entries:
        w.writerow([
            e.get("sequence", ""),
            e.get("commit", {}).get("short_hash", ""),
            e.get("commit", {}).get("date", ""),
            e.get("build", {}).get("status", ""),
            e.get("build", {}).get("exe_size_bytes", 0),
            e.get("build", {}).get("iso_size_bytes", 0),
            e.get("commit", {}).get("message", "")[:80],
        ])

print(f"Wrote index.json ({len(entries)} entries) and index.csv")
PYEOF

# Summary
cat > "$OUTPUT_DIR/SUMMARY.txt" << EOF
Binary Library Summary
======================
Generated: $(date -Iseconds)
Range:     $PS1_PORT_ORIGIN .. $END_REF
Total commits:     $TOTAL
Successful builds: $NUM_SUCCESS
Failed builds:     $NUM_FAILED
Skipped:           $NUM_SKIPPED
Resumed:           $NUM_RESUMED
Build time:        ${TOTAL_TIME}s ($(( TOTAL_TIME / 60 ))m $(( TOTAL_TIME % 60 ))s)
Output:            $OUTPUT_DIR
EOF

cat "$OUTPUT_DIR/SUMMARY.txt"
echo ""
echo "Library written to: $OUTPUT_DIR"
