#!/usr/bin/env python3
"""
Convert WAV files to PS1 VAG (Very Audio Good) format.
Supports 8-bit unsigned and 16-bit signed mono WAV input.
Output is Sony VAG format with 4-bit ADPCM encoding for PS1 SPU.
"""

import struct
import wave
import sys
import os

# PS1 SPU ADPCM filter coefficients (5 pairs)
FILTER_COEFF = [
    (0.0, 0.0),
    (60.0 / 64.0, 0.0),
    (115.0 / 64.0, -52.0 / 64.0),
    (98.0 / 64.0, -55.0 / 64.0),
    (122.0 / 64.0, -60.0 / 64.0),
]


def encode_vag(pcm_samples, sample_rate):
    """Encode 16-bit signed PCM samples to VAG ADPCM format."""
    # Pad samples to multiple of 28
    while len(pcm_samples) % 28 != 0:
        pcm_samples.append(0)

    num_blocks = len(pcm_samples) // 28
    adpcm_data = bytearray()

    # Add a silent block at the start (required by SPU)
    adpcm_data.extend(b'\x00' * 16)

    hist1 = 0.0
    hist2 = 0.0

    for block_idx in range(num_blocks):
        block_samples = pcm_samples[block_idx * 28:(block_idx + 1) * 28]

        # Try each filter and shift combination, pick the best
        best_error = float('inf')
        best_filter = 0
        best_shift = 0
        best_nibbles = []

        for filt in range(5):
            coeff1, coeff2 = FILTER_COEFF[filt]

            for shift in range(13):
                test_hist1 = hist1
                test_hist2 = hist2
                nibbles = []
                total_error = 0

                for s in block_samples:
                    predicted = coeff1 * test_hist1 + coeff2 * test_hist2
                    diff = s - predicted
                    # SPU hardware decoder computes:
                    #   delta = (signed_nibble << 12) >> shift
                    # so the effective scale per nibble step is 2^(12-shift),
                    # NOT 2^shift. Invert the shift here so the shift byte we
                    # store actually lines up with what the decoder does.
                    scale = 1 << (12 - shift)
                    nibble = int(round(diff / scale))
                    nibble = max(-8, min(7, nibble))
                    nibbles.append(nibble & 0xF)

                    # Reconstruct using the exact hardware formula so the
                    # error we accumulate matches what will be heard.
                    reconstructed = ((nibble << 12) >> shift) + predicted
                    reconstructed = max(-32768, min(32767, reconstructed))
                    error = abs(s - reconstructed)
                    total_error += error

                    test_hist2 = test_hist1
                    test_hist1 = reconstructed

                if total_error < best_error:
                    best_error = total_error
                    best_filter = filt
                    best_shift = shift
                    best_nibbles = nibbles
                    best_hist1 = test_hist1
                    best_hist2 = test_hist2

        # Commit the globally best (filter, shift)'s history as the starting
        # history for the next block. (This line was previously indented into
        # the `for filt` loop, which contaminated each filter's search with
        # the previous filter's reconstructed history.)
        hist1 = best_hist1
        hist2 = best_hist2

        # Build the 16-byte ADPCM block
        # Byte 0: shift | (filter << 4)
        # Byte 1: flags (0=normal, 1=loop end, 3=loop start, 7=end of data)
        flags = 0
        if block_idx == num_blocks - 1:
            flags = 1  # End flag

        block = bytearray(16)
        block[0] = (best_shift & 0xF) | ((best_filter & 0xF) << 4)
        block[1] = flags

        for i in range(0, 28, 2):
            # SPU ADPCM: low nibble is the earlier-in-time sample, high nibble
            # is the later one. Reversing this still decodes to a waveform with
            # the right envelope but swaps every adjacent sample pair,
            # producing high-frequency hash over the real signal.
            lo = best_nibbles[i] & 0xF
            hi = best_nibbles[i + 1] & 0xF
            block[2 + i // 2] = (hi << 4) | lo

        adpcm_data.extend(block)

    # Add end block
    end_block = bytearray(16)
    end_block[0] = 0
    end_block[1] = 7  # End + stop flag
    adpcm_data.extend(end_block)

    return bytes(adpcm_data)


def wav_to_vag(wav_path, vag_path):
    """Convert a WAV file to VAG format."""
    with wave.open(wav_path, 'rb') as w:
        channels = w.getnchannels()
        sample_width = w.getsampwidth()
        sample_rate = w.getframerate()
        num_frames = w.getnframes()
        raw_data = w.readframes(num_frames)

    if channels != 1:
        print(f"  Warning: {wav_path} has {channels} channels, using first only")

    # Convert to 16-bit signed samples
    samples = []
    if sample_width == 1:
        # 8-bit unsigned -> 16-bit signed
        for i in range(0, len(raw_data), channels):
            val = raw_data[i]
            samples.append((val - 128) * 256)
    elif sample_width == 2:
        # 16-bit signed
        for i in range(0, len(raw_data), 2 * channels):
            val = struct.unpack_from('<h', raw_data, i)[0]
            samples.append(val)
    else:
        print(f"  Error: unsupported sample width {sample_width}")
        return False

    # Encode to ADPCM
    adpcm_data = encode_vag(samples, sample_rate)

    # Build VAG header (48 bytes)
    name = os.path.splitext(os.path.basename(wav_path))[0][:15]
    header = bytearray(48)
    # Magic "VAGp"
    header[0:4] = b'VAGp'
    # Version (big-endian)
    struct.pack_into('>I', header, 4, 0x00000020)
    # Reserved
    struct.pack_into('>I', header, 8, 0)
    # Data size (big-endian) - size of ADPCM data after header
    struct.pack_into('>I', header, 12, len(adpcm_data))
    # Sample rate (big-endian)
    struct.pack_into('>I', header, 16, sample_rate)
    # Reserved bytes 20-31
    # Name at offset 32 (16 bytes)
    name_bytes = name.encode('ascii')[:16]
    header[32:32 + len(name_bytes)] = name_bytes

    with open(vag_path, 'wb') as f:
        f.write(header)
        f.write(adpcm_data)

    return True


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <input.wav> <output.vag>")
        print(f"       {sys.argv[0]} --batch <src_dir> <out_dir>")
        sys.exit(1)

    if sys.argv[1] == '--batch':
        src_dir = sys.argv[2]
        out_dir = sys.argv[3]
        os.makedirs(out_dir, exist_ok=True)

        converted = 0
        for i in range(25):
            src = os.path.join(src_dir, f'sound{i}.wav')
            dst = os.path.join(out_dir, f'SOUND{i:02d}.VAG')
            if not os.path.exists(src):
                print(f"  SKIP: sound{i}.wav (not found)")
                continue
            print(f"  Converting: sound{i}.wav -> SOUND{i:02d}.VAG")
            if wav_to_vag(src, dst):
                converted += 1

        print(f"\nConverted {converted} files to {out_dir}/")
    else:
        wav_to_vag(sys.argv[1], sys.argv[2])


if __name__ == '__main__':
    main()
