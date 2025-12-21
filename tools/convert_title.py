#!/usr/bin/env python3
"""
Convert INTRO.SCR + JOHNCAST.PAL to raw PS1 15-bit format
Output: TITLE.RAW - 640x480 raw 15-bit pixels (614400 bytes)
"""

import sys
import os

def main():
    scr_path = "jc_resources/extracted/scr/INTRO.SCR"
    pal_path = "jc_resources/extracted/pal/JOHNCAST.PAL"
    out_path = "TITLE.RAW"

    # Read palette (256 RGB triplets)
    with open(pal_path, "rb") as f:
        pal_data = f.read()

    # Convert VGA palette (6-bit per channel stored as bytes) to 15-bit PS1 format
    # VGA palette values are 0-63, we need to scale to 0-31 for PS1
    ps1_palette = []
    for i in range(256):
        # VGA palette stores 6-bit values in the low 6 bits of each byte
        r6 = pal_data[i * 3] & 0x3F       # 6-bit red (0-63)
        g6 = pal_data[i * 3 + 1] & 0x3F   # 6-bit green (0-63)
        b6 = pal_data[i * 3 + 2] & 0x3F   # 6-bit blue (0-63)

        # Scale from 6-bit (0-63) to 5-bit (0-31) - divide by 2
        r5 = r6 >> 1
        g5 = g6 >> 1
        b5 = b6 >> 1

        # PS1 15-bit format: 0BBBBBGGGGGRRRRR (BGR order, R in low bits)
        ps1_color = (b5 << 10) | (g5 << 5) | r5
        ps1_palette.append(ps1_color)

    # Read SCR file (4-bit indexed, packed nibbles)
    with open(scr_path, "rb") as f:
        scr_data = f.read()

    print(f"SCR size: {len(scr_data)} bytes")
    print(f"Expected: 153600 bytes (640x480/2)")

    # Convert to 15-bit pixels
    width = 640
    height = 480
    output = bytearray(width * height * 2)  # 2 bytes per pixel

    for y in range(height):
        for x in range(width):
            # Get palette index from 4-bit packed data
            byte_offset = (y * width + x) // 2
            if byte_offset >= len(scr_data):
                pal_idx = 0
            else:
                byte_val = scr_data[byte_offset]
                if (x & 1) == 0:
                    pal_idx = byte_val >> 4    # High nibble first (even pixels)
                else:
                    pal_idx = byte_val & 0x0F  # Low nibble second (odd pixels)

            # Get 15-bit color from palette
            color = ps1_palette[pal_idx]

            # Write as little-endian 16-bit
            out_offset = (y * width + x) * 2
            output[out_offset] = color & 0xFF
            output[out_offset + 1] = (color >> 8) & 0xFF

    # Write output
    with open(out_path, "wb") as f:
        f.write(output)

    print(f"Wrote {len(output)} bytes to {out_path}")
    print(f"Expected: {width * height * 2} bytes")

if __name__ == "__main__":
    main()
