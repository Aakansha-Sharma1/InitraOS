#!/usr/bin/env python3
"""
InitraOS automated boot test.

Boots build/disk.img in QEMU with no display, captures the COM1 serial log,
and asserts that the expected boot markers appear in order. Exits non-zero if
the OS fails to reach the current milestone's success marker.

Requires an image built with -DAUTOBOOT.
"""

import os
import subprocess
import sys


HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD = os.path.join(ROOT, "build")
IMAGE = os.path.join(BUILD, "disk.img")
LOG = os.path.join(BUILD, "serial.log")

QEMU = os.environ.get("QEMU", "qemu-system-x86_64")
TIMEOUT = int(os.environ.get("BOOT_TIMEOUT", "20"))


# ---------------------------------------------------------------------------
# Expected markers
#
# Milestone 10 / Issue #80:
#   The kernel prepares the 64-bit paging hierarchy, enables PAE,
#   loads CR3 with the PML4, enables EFER.LME, enables CR0.PG,
#   and confirms successful long-mode activation.
#
# The kernel intentionally halts immediately after LONG_MODE_ENABLED_OK.
# Later interrupt/user-mode transition work belongs to subsequent issues.
# ---------------------------------------------------------------------------

EXPECTED = [
    "S1:LBA",
    "[InitraOS] kernel entry, serial online",
    "[InitraOS] CPU vendor:",
    "[InitraOS] IDT loaded",
    "[InitraOS] PIC remapped, PIT armed",
    "[InitraOS] INT0 handler reached",
    "[InitraOS] IRET returned to kernel",
    "[InitraOS] BOOT_OK",
    "[InitraOS] E820 entries:",
    "[InitraOS] FRAME_ALLOC_OK",
    "[InitraOS] FRAME_TRACK_OK",
    "[InitraOS] FRAME_FREE_OK",
    "[InitraOS] FRAME_REUSE_OK",
    "[InitraOS] FRAME_PROTECT_OK",
    "[InitraOS] FRAME_VALIDATION_OK",
    "[InitraOS] ADDRESS_SPACE_CREATE_OK",
    "[InitraOS] FRAME_PAGING_OK",
    "[InitraOS] DYNAMIC_PAGE_OK",
    "[InitraOS] PAGE_PROTECTION_OK",
    "[InitraOS] PAGE_USER_PROTECTION_OK",
    "[InitraOS] HEAP_PAGING_OK",
    "[InitraOS] HEAP_DYNAMIC_OK",
    "[InitraOS] BEFORE_PG",
    "[InitraOS] LONG_MODE_ENABLED_OK",
    "[InitraOS] KERNEL64_ENTRY_OK",
    "[InitraOS] KERNEL64_INT0_OK",
    "[InitraOS] KERNEL64_TIMER_IRQ_OK",
]


def main():
    if not os.path.exists(IMAGE):
        sys.exit(
            f"ERROR: {IMAGE} not found - run 'make' first"
        )

    if os.path.exists(LOG):
        os.remove(LOG)

    cmd = [
        QEMU,
        "-drive",
        f"file={IMAGE},format=raw,if=ide",
        "-serial",
        f"file:{LOG}",
        "-display",
        "none",
        "-no-reboot",
    ]

    print("booting:", " ".join(cmd))

    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )

    try:
        # The kernel halts forever on successful Issue #80 validation,
        # so reaching the timeout is the normal success path.
        _, err = proc.communicate(timeout=TIMEOUT)

        if proc.returncode != 0:
            print(
                err.decode(errors="replace"),
                file=sys.stderr,
            )

    except subprocess.TimeoutExpired:
        proc.kill()
        proc.communicate()

    if not os.path.exists(LOG):
        sys.exit(
            "FAIL: QEMU produced no serial log at all"
        )

    with open(LOG, "r", errors="replace") as f:
        output = f.read()

    print("-" * 62)
    print("SERIAL OUTPUT")
    print("-" * 62)
    print(output.strip() or "(empty)")
    print("-" * 62)

    position = 0
    missing = []

    for marker in EXPECTED:
        index = output.find(marker, position)

        if index < 0:
            missing.append(marker)
        else:
            position = index + len(marker)

    if missing:
        print(
            "FAIL: missing or out-of-order boot markers:",
            file=sys.stderr,
        )

        for marker in missing:
            print(
                "  * " + marker,
                file=sys.stderr,
            )

        sys.exit(1)

    print(
        "PASS: all %d boot markers present and in order"
        % len(EXPECTED)
    )


if __name__ == "__main__":
    main()
