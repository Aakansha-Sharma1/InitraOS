#!/usr/bin/env python3
"""
InitraOS disk image builder.

Assembles boot.bin + stage2.bin + kernel.bin into a 1.44MB floppy image and,
critically, refuses to produce an image where the kernel would be silently
truncated by the bootloader.

The bootloader reads a fixed number of sectors into a fixed window at 0x8000.
If stage2 + kernel outgrow that window there is NO error at runtime: the
kernel is loaded half-complete and executes garbage. This script makes that
a loud build-time failure instead, and recomputes the sector count so the
bootloader always loads exactly what is needed.

Usage:
    python tools/mkimage.py --sectors-only     # print sectors needed, exit
    python tools/mkimage.py                    # build disk.img
"""

import argparse
import os
import sys

SECTOR_SIZE = 512
FLOPPY_SIZE = 1474560           # 1.44MB
LOAD_BASE = 0x8000              # where stage2 is loaded
STAGE2_MAX = 2048               # stage2.bin is padded to this
SECTORS_PER_TRACK = 18          # BIOS CHS geometry limit per track

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD = os.path.join(ROOT, "build")


def read(name):
    path = os.path.join(BUILD, name)
    if not os.path.exists(path):
        sys.exit(f"ERROR: {path} not found - assemble it first")
    with open(path, "rb") as f:
        return f.read()


def sectors_needed():
    stage2 = read("stage2.bin")
    kernel = read("kernel.bin")
    total = len(stage2) + len(kernel)
    return (total + SECTOR_SIZE - 1) // SECTOR_SIZE, len(stage2), len(kernel)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sectors-only", action="store_true")
    args = ap.parse_args()

    need, s2_len, k_len = sectors_needed()

    if args.sectors_only:
        print(need)
        return

    boot = read("boot.bin")
    stage2 = read("stage2.bin")
    kernel = read("kernel.bin")

    # ---- guard rails -------------------------------------------------
    errors = []

    if len(boot) != SECTOR_SIZE:
        errors.append(f"boot.bin is {len(boot)} bytes, must be exactly 512")

    if boot[510:512] != b"\x55\xAA":
        errors.append("boot.bin is missing the 0xAA55 boot signature")

    if len(stage2) != STAGE2_MAX:
        errors.append(
            f"stage2.bin is {len(stage2)} bytes, expected exactly {STAGE2_MAX} "
            f"(it is padded with 'times {STAGE2_MAX}-($-$$) db 0'; if it "
            f"overflowed, NASM would have errored, so this means the pad "
            f"constant changed)"
        )

    if need > SECTORS_PER_TRACK - 1:
        errors.append(
            f"kernel needs {need} sectors, but the bootloader's CHS read "
            f"starts at LBA 1 and a 1.44MB floppy track holds only "
            f"{SECTORS_PER_TRACK}. Move to a real ELF loader (Phase 1)."
        )

    if errors:
        print("=" * 62, file=sys.stderr)
        print("BUILD FAILED", file=sys.stderr)
        print("=" * 62, file=sys.stderr)
        for e in errors:
            print("  * " + e, file=sys.stderr)
        sys.exit(1)

    # ---- assemble ----------------------------------------------------
    image = bytearray(FLOPPY_SIZE)
    image[0:len(boot)] = boot
    image[SECTOR_SIZE:SECTOR_SIZE + len(stage2)] = stage2
    kernel_off = SECTOR_SIZE + len(stage2)
    image[kernel_off:kernel_off + len(kernel)] = kernel

    out = os.path.join(BUILD, "disk.img")
    with open(out, "wb") as f:
        f.write(image)

    window = need * SECTOR_SIZE
    used = s2_len + k_len
    kernel_addr = LOAD_BASE + s2_len

    print("-" * 62)
    print("InitraOS image built")
    print("-" * 62)
    print(f"  boot.bin     {len(boot):>6} bytes   LBA 0        -> 0x7C00")
    print(f"  stage2.bin   {s2_len:>6} bytes   LBA 1         -> 0x{LOAD_BASE:04X}")
    print(f"  kernel.bin   {k_len:>6} bytes   LBA {kernel_off // SECTOR_SIZE}"
          f"         -> 0x{kernel_addr:04X}")
    print(f"  load window  {window:>6} bytes   ({need} sectors)")
    print(f"  used         {used:>6} bytes")
    print(f"  headroom     {window - used:>6} bytes")
    print(f"  kernel limit {window - s2_len:>6} bytes before truncation")
    print(f"  -> {out}")
    print("-" * 62)


if __name__ == "__main__":
    main()
