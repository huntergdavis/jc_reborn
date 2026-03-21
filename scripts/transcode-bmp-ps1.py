#!/usr/bin/env python3
"""
Transcode a Sierra BMP sprite sheet into PSB (PS1 Sprite Bundle) format.

The PSB format pre-swaps nibbles from Sierra order (HIGH=pixel0) to PS1 order
(LOW=pixel0), eliminating per-frame runtime nibble swapping on the PS1.

Usage:
    python3 scripts/transcode-bmp-ps1.py JOHNWALK.BMP
    python3 scripts/transcode-bmp-ps1.py --all
    python3 scripts/transcode-bmp-ps1.py JOHNWALK.BMP --verify

Requires RESOURCE.MAP and RESOURCE.001 for frame metadata, plus the
pre-extracted BMP files in jc_resources/extracted/bmp/.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

# --- PSB format constants (must match psb_format.h) ---
PSB_MAGIC   = 0x31425350  # "PSB1" little-endian
PSB_VERSION = 1

# --- Sierra BMP resource file parsing ---

def find_project_root() -> Path:
    """Walk up from script location to find project root with RESOURCE.MAP."""
    p = Path(__file__).resolve().parent.parent
    # Check if this directory has the full resource files
    if (p / "jc_resources" / "RESOURCE.MAP").exists():
        return p
    # Worktree: jc_resources/ exists but RESOURCE.MAP may be in the main repo.
    # Try to resolve via git worktree linkage.
    git_dir = p / ".git"
    if git_dir.is_file():
        # .git is a file pointing to the real git dir in worktrees
        text = git_dir.read_text().strip()
        if text.startswith("gitdir:"):
            real_git = Path(text.split(":", 1)[1].strip()).resolve()
            # real_git is like /repo/.git/worktrees/xxx -> main repo is /repo
            main_repo = real_git.parent.parent.parent
            if (main_repo / "jc_resources" / "RESOURCE.MAP").exists():
                return main_repo
    # Fallback: walk up
    for candidate in [p, p.parent, p.parent.parent]:
        if (candidate / "jc_resources" / "RESOURCE.MAP").exists():
            return candidate
    raise FileNotFoundError(
        "Cannot find RESOURCE.MAP. Use --resource-dir to specify the "
        "directory containing RESOURCE.MAP and RESOURCE.001"
    )


def read_uint8(f) -> int:
    return struct.unpack("<B", f.read(1))[0]

def read_uint16(f) -> int:
    return struct.unpack("<H", f.read(2))[0]

def read_uint32(f) -> int:
    return struct.unpack("<I", f.read(4))[0]

def read_string(f, length: int) -> str:
    raw = f.read(length)
    # Null-terminated
    idx = raw.find(b'\x00')
    if idx >= 0:
        raw = raw[:idx]
    return raw.decode("ascii", errors="replace")


class BmpMeta:
    """Metadata for one Sierra BMP resource, parsed from RESOURCE.MAP/001."""
    def __init__(self, name: str, num_images: int, widths: list[int],
                 heights: list[int], uncompressed_size: int,
                 compression_method: int, compressed_size: int,
                 data_offset: int):
        self.name = name
        self.num_images = num_images
        self.widths = widths
        self.heights = heights
        self.uncompressed_size = uncompressed_size
        self.compression_method = compression_method
        self.compressed_size = compressed_size
        self.data_offset = data_offset  # offset of compressed data in RESOURCE.001


def parse_resource_index(project_root: Path) -> dict[str, BmpMeta]:
    """Parse RESOURCE.MAP + RESOURCE.001 to extract BMP metadata."""
    map_path = project_root / "jc_resources" / "RESOURCE.MAP"
    res_path = project_root / "jc_resources" / "RESOURCE.001"

    if not map_path.exists():
        raise FileNotFoundError(f"RESOURCE.MAP not found at {map_path}")
    if not res_path.exists():
        raise FileNotFoundError(f"RESOURCE.001 not found at {res_path}")

    # Parse RESOURCE.MAP
    with open(map_path, "rb") as f:
        # Skip 6 unknown header bytes
        f.read(6)
        # Read resource filename (13 bytes)
        res_filename = read_string(f, 13)
        num_entries = read_uint16(f)

        entries = []
        for _ in range(num_entries):
            length = read_uint32(f)
            offset = read_uint32(f)
            entries.append((length, offset))

    # Parse RESOURCE.001 to find BMP resources
    bmps: dict[str, BmpMeta] = {}
    with open(res_path, "rb") as f:
        for length, offset in entries:
            f.seek(offset)
            res_name = read_string(f, 13)
            res_size = read_uint32(f)

            if not res_name.upper().endswith(".BMP"):
                continue

            # Parse BMP header: "BMP:" + width + height
            tag = f.read(4)
            if tag != b"BMP:":
                continue
            bmp_width = read_uint16(f)
            bmp_height = read_uint16(f)

            # Parse INF header
            tag = f.read(4)
            if tag != b"INF:":
                continue
            data_size = read_uint32(f)
            num_images = read_uint16(f)

            widths = [read_uint16(f) for _ in range(num_images)]
            heights = [read_uint16(f) for _ in range(num_images)]

            # Parse BIN header
            tag = f.read(4)
            if tag != b"BIN:":
                continue
            bin_size = read_uint32(f) - 5  # subtract compression header
            compression_method = read_uint8(f)
            uncompressed_size = read_uint32(f)

            data_offset = f.tell()

            bmps[res_name.upper()] = BmpMeta(
                name=res_name,
                num_images=num_images,
                widths=widths,
                heights=heights,
                uncompressed_size=uncompressed_size,
                compression_method=compression_method,
                compressed_size=bin_size,
                data_offset=data_offset,
            )

    return bmps


def swap_nibbles(data: bytes) -> bytes:
    """Swap nibbles in each byte: Sierra (HIGH=pixel0) -> PS1 (LOW=pixel0)."""
    out = bytearray(len(data))
    for i, b in enumerate(data):
        out[i] = ((b & 0x0F) << 4) | ((b >> 4) & 0x0F)
    return bytes(out)


def align4(size: int) -> int:
    """Round up to 4-byte alignment."""
    return (size + 3) & ~3


def transcode_bmp(meta: BmpMeta, pixel_data: bytes, verify: bool = False) -> bytes:
    """
    Convert Sierra BMP pixel data to PSB format.

    Args:
        meta: BMP metadata (frame dimensions)
        pixel_data: Raw decompressed Sierra BMP pixel data
        verify: If True, verify round-trip correctness

    Returns:
        Complete PSB file as bytes
    """
    num_frames = meta.num_images
    header_size = 16  # PSBHeader
    frame_table_size = 12 * num_frames  # PSBFrame entries
    data_offset = align4(header_size + frame_table_size)

    # Build frame descriptors and transcoded pixel data
    frames = []
    pixel_chunks = []
    current_data_offset = 0
    src_offset = 0

    for i in range(num_frames):
        w = meta.widths[i]
        h = meta.heights[i]
        frame_pixels = (w * h + 1) // 2  # 4-bit packed size

        if src_offset + frame_pixels > len(pixel_data):
            print(f"  WARNING: Frame {i} would read past pixel data "
                  f"(need {src_offset + frame_pixels}, have {len(pixel_data)})",
                  file=sys.stderr)
            break

        sierra_chunk = pixel_data[src_offset:src_offset + frame_pixels]
        ps1_chunk = swap_nibbles(sierra_chunk)

        if verify:
            # Verify round-trip: swapping again should give back original
            roundtrip = swap_nibbles(ps1_chunk)
            if roundtrip != sierra_chunk:
                print(f"  ERROR: Round-trip verification failed for frame {i}!",
                      file=sys.stderr)
                sys.exit(1)

        aligned_size = align4(len(ps1_chunk))
        # Pad to alignment
        padded_chunk = ps1_chunk + b'\x00' * (aligned_size - len(ps1_chunk))

        frames.append({
            'width': w,
            'height': h,
            'offset': current_data_offset,
            'size': aligned_size,
        })
        pixel_chunks.append(padded_chunk)

        current_data_offset += aligned_size
        src_offset += frame_pixels

    actual_frames = len(frames)

    # Assemble PSB file
    total_pixel_data = b''.join(pixel_chunks)
    total_size = data_offset + len(total_pixel_data)

    # Write header
    header = struct.pack("<IHHII",
                         PSB_MAGIC,
                         PSB_VERSION,
                         actual_frames,
                         data_offset,
                         total_size)

    # Write frame table
    frame_table = b''
    for fr in frames:
        frame_table += struct.pack("<HHII",
                                   fr['width'],
                                   fr['height'],
                                   fr['offset'],
                                   fr['size'])

    # Padding between frame table and data
    padding_needed = data_offset - header_size - len(frame_table)
    padding = b'\x00' * padding_needed

    psb_data = header + frame_table + padding + total_pixel_data

    assert len(psb_data) == total_size, f"Size mismatch: {len(psb_data)} != {total_size}"
    return psb_data


def print_stats(name: str, meta: BmpMeta, original_size: int, psb_size: int):
    """Print comparison statistics."""
    print(f"\n{'='*60}")
    print(f"  Transcoded: {name}")
    print(f"{'='*60}")
    print(f"  Frames:          {meta.num_images}")
    print(f"  Max dimensions:  {max(meta.widths)}x{max(meta.heights)}")
    print(f"  Original BMP:    {original_size:,} bytes (Sierra nibble order)")
    print(f"  PSB output:      {psb_size:,} bytes (PS1 nibble order)")
    overhead = psb_size - original_size
    pct = (overhead / original_size * 100) if original_size > 0 else 0
    print(f"  Header overhead: {overhead:+,} bytes ({pct:+.1f}%)")
    print(f"  RAM footprint:   {psb_size:,} bytes (loaded as-is, zero-copy frames)")
    print()

    # Per-frame breakdown
    print(f"  {'Frame':>5}  {'W':>4} x {'H':>4}  {'Pixels':>7}  {'Packed':>7}")
    print(f"  {'-'*5}  {'-'*4}   {'-'*4}  {'-'*7}  {'-'*7}")
    for i in range(meta.num_images):
        w, h = meta.widths[i], meta.heights[i]
        pixels = w * h
        packed = (pixels + 1) // 2
        print(f"  {i:>5}  {w:>4} x {h:>4}  {pixels:>7}  {packed:>7}")
    print()


def main():
    parser = argparse.ArgumentParser(
        description="Transcode Sierra BMP to PSB (PS1 Sprite Bundle)")
    parser.add_argument("bmp_name", nargs="?",
                        help="BMP resource name (e.g. JOHNWALK.BMP)")
    parser.add_argument("--all", action="store_true",
                        help="Transcode all BMP resources")
    parser.add_argument("--verify", action="store_true",
                        help="Verify round-trip nibble-swap correctness")
    parser.add_argument("--output-dir", type=Path,
                        help="Output directory (default: jc_resources/transcoded)")
    parser.add_argument("--extracted-dir", type=Path,
                        help="Extracted BMP directory")
    parser.add_argument("--resource-dir", type=Path,
                        help="Directory containing RESOURCE.MAP and RESOURCE.001")
    parser.add_argument("--list", action="store_true",
                        help="List all available BMP resources and exit")
    args = parser.parse_args()

    if args.resource_dir:
        project_root = args.resource_dir.parent  # assume resource-dir IS jc_resources
        resource_root = args.resource_dir
    else:
        project_root = find_project_root()
        resource_root = None  # use default

    # For extracted dir and output dir, use the script's own project (worktree)
    script_project = Path(__file__).resolve().parent.parent
    extracted_dir = args.extracted_dir or (script_project / "jc_resources" / "extracted" / "bmp")
    output_dir = args.output_dir or (script_project / "jc_resources" / "transcoded")

    if not extracted_dir.is_dir():
        print(f"ERROR: Extracted BMP directory not found: {extracted_dir}", file=sys.stderr)
        sys.exit(1)

    print(f"Parsing resource index from {project_root / 'jc_resources'}...")
    bmps = parse_resource_index(project_root)
    print(f"Found {len(bmps)} BMP resources in RESOURCE.MAP/001")

    if args.list:
        print(f"\n{'Name':<20} {'Frames':>6} {'Uncomp':>8} {'MaxW':>5} {'MaxH':>5}")
        print(f"{'-'*20} {'-'*6} {'-'*8} {'-'*5} {'-'*5}")
        for name in sorted(bmps.keys()):
            m = bmps[name]
            mw = max(m.widths) if m.widths else 0
            mh = max(m.heights) if m.heights else 0
            print(f"{name:<20} {m.num_images:>6} {m.uncompressed_size:>8} {mw:>5} {mh:>5}")
        return

    if not args.bmp_name and not args.all:
        parser.print_help()
        sys.exit(1)

    output_dir.mkdir(parents=True, exist_ok=True)

    # Determine which BMPs to transcode
    if args.all:
        targets = sorted(bmps.keys())
    else:
        name = args.bmp_name.upper()
        if not name.endswith(".BMP"):
            name += ".BMP"
        if name not in bmps:
            print(f"ERROR: BMP resource '{name}' not found in RESOURCE.MAP", file=sys.stderr)
            print(f"Available: {', '.join(sorted(bmps.keys())[:10])}...", file=sys.stderr)
            sys.exit(1)
        targets = [name]

    total_original = 0
    total_psb = 0
    transcoded_count = 0

    for name in targets:
        meta = bmps[name]
        bmp_path = extracted_dir / name

        if not bmp_path.exists():
            print(f"  SKIP: {name} (no extracted file at {bmp_path})")
            continue

        pixel_data = bmp_path.read_bytes()
        original_size = len(pixel_data)

        psb_data = transcode_bmp(meta, pixel_data, verify=args.verify)
        psb_size = len(psb_data)

        # Output filename: JOHNWALK.BMP -> JOHNWALK.PSB
        psb_name = name.replace(".BMP", ".PSB")
        psb_path = output_dir / psb_name
        psb_path.write_bytes(psb_data)

        print_stats(name, meta, original_size, psb_size)

        if args.verify:
            print(f"  VERIFIED: Round-trip nibble swap correct for all {meta.num_images} frames")

        total_original += original_size
        total_psb += psb_size
        transcoded_count += 1

    if transcoded_count > 1:
        print(f"\n{'='*60}")
        print(f"  TOTALS: {transcoded_count} files transcoded")
        print(f"  Original:    {total_original:,} bytes")
        print(f"  Transcoded:  {total_psb:,} bytes")
        overhead = total_psb - total_original
        pct = (overhead / total_original * 100) if total_original > 0 else 0
        print(f"  Overhead:    {overhead:+,} bytes ({pct:+.1f}%)")
        print(f"{'='*60}")

    if transcoded_count > 0:
        print(f"\nOutput written to: {output_dir}/")


if __name__ == "__main__":
    main()
