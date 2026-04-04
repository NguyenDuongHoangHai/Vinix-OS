#!/usr/bin/env python3
"""
gp_header.py
Generate MLO boot image for AM335x ROM code (GP device)

MLO format: TOC (512 bytes) + GP header (8 bytes) + executable code

Same file works for both boot paths:
  - RAW mode: dd to raw SD card sectors (0x20000, 0x40000, 0x60000)
  - FAT mode: copy as "MLO" file on FAT partition

ROM code checks for TOC/CHSETTINGS first in both modes.
"""

import struct
import sys
import os

# Load address in SRAM (OCMC RAM start per AM335x BootROM)
# BootROM loads GP image to 0x402F0400 (Public RAM area, TRM sec 26.1.4.2)
LOAD_ADDR = 0x402F0400

# BootROM OCMC RAM (public) = 111616 bytes (109 KB)
# TRM sec 26.1.9.2: "maximum size of downloaded image is 109 KB"
MAX_SIZE = 109 * 1024


def make_toc():
    """
    Create 512-byte TOC (Table of Contents) with CHSETTINGS.

    TRM sec 26.1.11 — TOC format for GP device:
      Offset 0x00: TOC Item 1 (32 bytes) — points to CHSETTINGS
      Offset 0x20: TOC Item 2 (32 bytes) — 0xFF terminator
      Offset 0x40: CHSETTINGS magic values (Table 26-39)
      Rest:        zeros

    @return  512-byte TOC block
    """
    toc = bytearray(512)

    # TOC Item 1 — CHSETTINGS (Table 26-38)
    toc_item = struct.pack('<III',
        0x00000040,     # Start: offset to CHSETTINGS data within TOC
        0x0000000C,     # Size: 12 bytes of CHSETTINGS data
        0x00000000,     # Flags: not used
    )
    toc_item += struct.pack('<II',
        0x00000000,     # Align: not used
        0x00000000,     # Load Address: not used
    )
    # Filename: "CHSETTINGS\0" (12 bytes, null-terminated)
    toc_item += b'CHSETTINGS\x00\x00'

    toc[0x00:0x00 + len(toc_item)] = toc_item

    # TOC Item 2 — terminator (all 0xFF)
    toc[0x20:0x40] = b'\xFF' * 32

    # CHSETTINGS magic values (Table 26-39)
    struct.pack_into('<II', toc, 0x40,
        0xC0C0C0C1,    # Magic value 1
        0x00000100,    # Magic value 2
    )

    return bytes(toc)


def make_gp_header(code_size):
    """
    Create 8-byte GP header per TRM Table 26-37.

    @param code_size  Size of executable code in bytes
    @return           8-byte GP header (size + destination, little-endian)
    """
    return struct.pack('<II', code_size, LOAD_ADDR)


def make_mlo(input_bin, output_mlo):
    """
    Generate MLO = TOC (512) + GP header (8) + code.

    @param input_bin   Path to bootloader.bin (raw binary)
    @param output_mlo  Path to output MLO file
    @return            0 on success, 1 on error
    """
    with open(input_bin, 'rb') as f:
        bootloader = f.read()

    code_size = len(bootloader)
    toc = make_toc()
    gp_header = make_gp_header(code_size)

    with open(output_mlo, 'wb') as f:
        f.write(toc)        # 512 bytes: TOC with CHSETTINGS
        f.write(gp_header)  # 8 bytes:   GP header (size + destination)
        f.write(bootloader) # N bytes:   executable code

    total_size = len(toc) + len(gp_header) + code_size
    print(f"Generated {output_mlo}:")
    print(f"  Bootloader size: {code_size} bytes")
    print(f"  Total MLO size:  {total_size} bytes (512 TOC + 8 GP + {code_size} code)")
    print(f"  Load address:    0x{LOAD_ADDR:08X}")

    if code_size > MAX_SIZE:
        print(f"  WARNING: Image ({code_size} bytes) exceeds SRAM limit ({MAX_SIZE} bytes)!")
        return 1
    else:
        print(f"  SRAM usage:      {code_size}/{MAX_SIZE} bytes ({code_size * 100 // MAX_SIZE}%)")

    return 0


def main():
    if len(sys.argv) != 3:
        print("Usage: gp_header.py <input.bin> <output.MLO>")
        print("")
        print("Example:")
        print("  python3 gp_header.py bootloader.bin MLO")
        return 1

    input_bin = sys.argv[1]
    output_mlo = sys.argv[2]

    if not os.path.exists(input_bin):
        print(f"Error: Input file '{input_bin}' not found")
        return 1

    return make_mlo(input_bin, output_mlo)


if __name__ == '__main__':
    sys.exit(main())
