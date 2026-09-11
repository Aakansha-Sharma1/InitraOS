#!/usr/bin/env python3
"""
InitraOS disk image builder.

Assembles boot.bin + stage2.bin + kernel.bin into a hard disk image and,
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
DISK_SIZE = 16 * 1024 * 1024    # 16MB hard disk image
LOAD_BASE = 0x8000              # where stage2 is loaded
STAGE2_MAX = 2048               # stage2.bin is padded to this

# The loader reads into real-mode conventional memory starting at LOAD_BASE,
# advancing the destination segment once per sector. 0x80000 is a deliberately
# conservative ceiling: the real barrier is the EBDA near 0x9FC00, and we stay
# well clear of it.
LOAD_CEILING = 0x80000
MAX_LOAD_SECTORS = (LOAD_CEILING - LOAD_BASE) // SECTOR_SIZE

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

    if need > MAX_LOAD_SECTORS:
        errors.append(
            f"stage2+kernel need {need} sectors ({need * SECTOR_SIZE} bytes), "
            f"which would load past 0x{LOAD_CEILING:X} in real mode. The "
            f"ceiling is {MAX_LOAD_SECTORS} sectors. Load the kernel into "
            f"extended memory after the switch to protected mode."
        )

    if errors:
        print("=" * 62, file=sys.stderr)
        print("BUILD FAILED", file=sys.stderr)
        print("=" * 62, file=sys.stderr)
        for e in errors:
            print("  * " + e, file=sys.stderr)
        sys.exit(1)

    # ---- assemble ----------------------------------------------------
    image = bytearray(DISK_SIZE)
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
    print(f"  load ceiling {MAX_LOAD_SECTORS:>6} sectors (0x{LOAD_CEILING:X})")
    print(f"  -> {out}")
    print("-" * 62)


if __name__ == "__main__":
    main()