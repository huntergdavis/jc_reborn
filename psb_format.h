/*
 *  PSB (PS1 Sprite Bundle) — pre-transcoded sprite sheet format
 *
 *  Moves Sierra BMP format complexity offline so the PS1 runtime can skip
 *  per-frame nibble swapping and directly LoadImage the pixel data.
 *
 *  FILE LAYOUT
 *  ===========
 *
 *  +-----------------------+
 *  | PSBHeader             |  (fixed 16 bytes)
 *  +-----------------------+
 *  | PSBFrame[numFrames]   |  (12 bytes each)
 *  +-----------------------+
 *  | <padding to 4 bytes>  |
 *  +-----------------------+
 *  | Frame 0 pixel data    |  (4-bit packed, PS1 nibble order, 4-byte aligned)
 *  | Frame 1 pixel data    |
 *  | ...                   |
 *  | Frame N pixel data    |
 *  +-----------------------+
 *
 *  PIXEL FORMAT
 *  ============
 *  4-bit indexed, two pixels per byte.
 *  PS1 native order: LOW nibble = pixel 0, HIGH nibble = pixel 1.
 *  (Sierra BMP uses the opposite: HIGH nibble = pixel 0.)
 *
 *  Each frame's data is padded to 4-byte alignment for DMA compatibility
 *  with the PS1 LoadImage command.
 *
 *  USAGE
 *  =====
 *  At load time the runtime reads the header + frame table, then points
 *  each PS1Surface's indexedPixels directly into the data region (zero-copy).
 *  The drawing path detects PSB data via a flag and skips nibble-swapping.
 *
 *  This file is part of 'Johnny Reborn' — PS1 Port.
 *  Licensed under the GNU GPL v3+.
 */

#ifndef PSB_FORMAT_H
#define PSB_FORMAT_H

#include <stdint.h>

#define PSB_MAGIC   0x31425350  /* "PSB1" in little-endian */
#define PSB_VERSION 1

/*
 * File header — 16 bytes, all fields little-endian.
 */
typedef struct PSBHeader {
    uint32_t magic;        /* PSB_MAGIC */
    uint16_t version;      /* PSB_VERSION */
    uint16_t numFrames;    /* Number of sprite frames */
    uint32_t dataOffset;   /* Byte offset from file start to first pixel data */
    uint32_t totalSize;    /* Total file size in bytes */
} PSBHeader;

/*
 * Per-frame descriptor — 12 bytes.
 * Immediately follows the header; numFrames entries.
 */
typedef struct PSBFrame {
    uint16_t width;        /* Frame width in pixels */
    uint16_t height;       /* Frame height in pixels */
    uint32_t offset;       /* Byte offset from dataOffset to this frame's pixels */
    uint32_t size;         /* Byte size of this frame's pixel data (4-byte aligned) */
} PSBFrame;

#endif /* PSB_FORMAT_H */
