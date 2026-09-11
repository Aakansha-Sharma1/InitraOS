#!/usr/bin/env python3
"""
InitraOS automated boot test.

Boots build/disk.img in QEMU with no display, captures the COM1 serial log,
and asserts that the expected boot markers appear in order. Exits non-zero if
the OS fails to reach BOOT_OK, so CI fails on a broken commit.

Requires an image built with -DAUTOBOOT (stage2 skips the interactive shell).
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD = os.path.join(ROOT, "build")
IMAGE = os.path.join(BUILD, "disk.img")
LOG = os.path.join(BUILD, "serial.log")

QEMU = os.environ.get("QEMU", "qemu-system-i386")
TIMEOUT = int(os.environ.get("BOOT_TIMEOUT", "20"))

# Markers that must appear on the serial port, in this order.
# "S1:LBA" comes from stage 1 and asserts that the INT 13h extended read
# path was actually taken. If the BIOS reported no EDD support the loader
# emits "S1:CHS" instead and this test fails, which is deliberate: a silent
# downgrade to the untested fallback path is something we want to be told
# about rather than discover later.
EXPECTED = [
    "S1:LBA",
    "[InitraOS] kernel entry, serial online",
    "[InitraOS] CPU vendor:",
    "[InitraOS] IDT loaded",
    "[InitraOS] PIC remapped, PIT armed",
    "[InitraOS] INT0 handler reached",
    "[InitraOS] IRET returned to kernel",
    "[InitraOS] BOOT_OK",
]


def main():
    if not os.path.exists(IMAGE):
        sys.exit(f"ERROR: {IMAGE} not found - run 'make' first")

    if os.path.exists(LOG):
        os.remove(LOG)

    cmd = [
        QEMU,
        "-drive", f"file={IMAGE},format=raw,if=ide",
        "-serial", f"file:{LOG}",
        "-display", "none",
        "-no-reboot",
    ]

    print("booting:", " ".join(cmd))
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                            stderr=subprocess.PIPE)
    try:
        # The kernel halts forever on success, so a timeout is the normal path.
        _, err = proc.communicate(timeout=TIMEOUT)
        if proc.returncode != 0:
            print(err.decode(errors="replace"), file=sys.stderr)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.communicate()

    if not os.path.exists(LOG):
        sys.exit("FAIL: QEMU produced no serial log at all")

    with open(LOG, "r", errors="replace") as f:
        output = f.read()

    print("-" * 62)
    print("SERIAL OUTPUT")
    print("-" * 62)
    print(output.strip() or "(empty)")
    print("-" * 62)

    pos = 0
    missing = []
    for marker in EXPECTED:
        idx = output.find(marker, pos)
        if idx < 0:
            missing.append(marker)
        else:
            pos = idx + len(marker)

    if missing:
        print("FAIL: missing or out-of-order boot markers:", file=sys.stderr)
        for m in missing:
            print("  * " + m, file=sys.stderr)
        sys.exit(1)

    print("PASS: all %d boot markers present and in order" % len(EXPECTED))


if __name__ == "__main__":
    main()
