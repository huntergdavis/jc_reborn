#!/bin/bash
# Convert WAV sound files to PS1 VAG format for SPU playback
# Usage: ./scripts/convert-sounds.sh
#
# Prerequisites:
#   - Original sound0.wav through sound24.wav in jc_resources/
#   - wav2vag tool (or ffmpeg + custom encoder)
#
# The original Sierra sounds are 22050 Hz, 8-bit unsigned, mono WAV files.
# PS1 SPU plays 4-bit ADPCM (VAG format).

set -e

cd "$(dirname "$0")/.."  # Change to project root

SRC_DIR="jc_resources"
OUT_DIR="jc_resources/extracted/snd"

mkdir -p "$OUT_DIR"

# Check if wav2vag is available in docker
WAV2VAG_CMD=""
if docker run --rm --platform linux/amd64 jc-reborn-ps1-dev:amd64 which wav2vag >/dev/null 2>&1; then
    WAV2VAG_CMD="docker"
    echo "Using wav2vag from Docker image"
elif command -v wav2vag >/dev/null 2>&1; then
    WAV2VAG_CMD="native"
    echo "Using native wav2vag"
else
    echo "ERROR: wav2vag not found. Install it or add to Docker image."
    echo ""
    echo "To install from source:"
    echo "  git clone https://github.com/psxdev/wav2vag.git"
    echo "  cd wav2vag && make && sudo cp wav2vag /usr/local/bin/"
    echo ""
    echo "Or use PSn00bSDK's built-in encoder if available."
    exit 1
fi

converted=0
missing=0

for i in $(seq 0 24); do
    src_file="${SRC_DIR}/sound${i}.wav"
    out_file="${OUT_DIR}/SOUND$(printf '%02d' $i).VAG"

    if [ ! -f "$src_file" ]; then
        echo "  SKIP: $src_file (not found)"
        missing=$((missing + 1))
        continue
    fi

    echo "  Converting: sound${i}.wav -> SOUND$(printf '%02d' $i).VAG"

    if [ "$WAV2VAG_CMD" = "docker" ]; then
        docker run --rm --platform linux/amd64 \
            -v "$PWD":/project \
            jc-reborn-ps1-dev:amd64 \
            wav2vag "/project/$src_file" "/project/$out_file"
    else
        wav2vag "$src_file" "$out_file"
    fi

    converted=$((converted + 1))
done

echo ""
echo "=== Conversion complete ==="
echo "  Converted: $converted files"
echo "  Missing:   $missing files"
echo "  Output:    $OUT_DIR/"

if [ $converted -gt 0 ]; then
    echo ""
    ls -lh "$OUT_DIR"/*.VAG 2>/dev/null
fi
