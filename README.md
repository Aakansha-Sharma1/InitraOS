# InitraOS

A 32-bit x86 operating system built from scratch in NASM assembly.

Current state: a two-stage bootloader, a 16-bit real-mode shell, and a
protected-mode kernel with GDT, IDT, PIC remapping, a PIT timer and serial
debug output. See `INITRAOS_ROADMAP.md` for where this is going.

## Building

### Windows (native)

Requires `nasm.exe` and `python` on PATH.

```bat
build.bat            :: build build\disk.img
build.bat run        :: build and boot in QEMU
build.bat clean      :: remove build artifacts
```

If NASM is not on PATH: `set NASM=C:\path\to\nasm.exe`

### Linux / WSL / CI

```sh
make                 # build build/disk.img
make run             # build and boot in QEMU (interactive)
make test            # build an autoboot image and assert on serial output
make clean
```

## Running

```sh
qemu-system-i386 -drive file=build/disk.img,format=raw,if=floppy -serial stdio
```

Serial output goes to your terminal. This is the primary debugging channel —
`serial_print` in the kernel writes to COM1.

## Shell commands

At the `INITRA>` prompt: `help`, `clear`, `version`, `echo <text>`, `sysinfo`,
`pmode` (switches to 32-bit protected mode and enters the kernel).

## Layout

| File | Purpose |
|---|---|
| `boot.asm` | Stage 1 bootloader (512 bytes). Loads stage2 + kernel sector by sector with retries. |
| `stage2.asm` | 16-bit real-mode shell. Enables A20 and switches to protected mode. |
| `kernel.asm` | 32-bit kernel: CPUID, IDT, PIC remap, PIT, ISRs. |
| `gdt.inc` | Flat 4 GB code/data descriptors. |
| `idt.inc` | 256-entry IDT with a 32-bit-safe gate macro. |
| `a20.inc` | A20 gate enable (BIOS, fast A20, keyboard controller fallback). |
| `serial.inc` | COM1 driver for debug output. |
| `tools/mkimage.py` | Builds `disk.img` and refuses to produce a truncated kernel. |
| `tools/boottest.py` | Boots the image in QEMU and asserts on serial markers. |

## Memory map (current)

```
0x07C00   stage 1 bootloader
0x08000   stage 2 (2048 bytes, fixed)
0x08800   kernel
0x90000   protected-mode stack
0xB8000   VGA text buffer
```

The bootloader loads a computed number of sectors into the window at
`0x8000`. `tools/mkimage.py` derives that count from the actual binary sizes
and fails the build if the kernel would not fit — a silent truncation here
produces a hang with no error message, so the guard is deliberate.

## Continuous integration

`.github/workflows/ci.yml` builds the image and boots it in real QEMU on every
push, asserting that the kernel reaches `BOOT_OK` on the serial port. The disk
image and serial log are uploaded as build artifacts.

## Notes

- The kernel currently reports the CPU base model only; extended model bits
  are not yet decoded, so a Haswell reports `0x0C` rather than `0x3C`.
  Fixed in Phase 1.
- Only vectors 0 and 32 have handlers. The rest are non-present.
- There is no memory manager, filesystem, or user mode yet.
