# OS Development State

## Project
I am building my own operating system as a learning/development project.

## Goal
Build the OS step-by-step and understand what I am implementing instead of
blindly generating code.

The long-term target is a desktop OS with a boot splash, a user login, and a
desktop environment. See `INITRAOS_ROADMAP.md` for the phased plan and honest
time estimates.

## Current situation
- The project is on GitHub. Branch `master`. Git push is working.
- Phase 0 is complete and merged (commit `6b88cab`).
- CI boots the OS in QEMU on every push and fails if it does not reach
  `BOOT_OK` on the serial port.
- Do NOT assume the current implementation from this file alone. Inspect the
  repository first.

## Important rule
GitHub repository = source of truth.

Before changing anything:
1. Inspect the repository.
2. Check the current Git status/history.
3. Understand what is already implemented.
4. Build/test the existing project.
5. Then suggest the next step.

## How I want you to work
- Explain things simply when needed.
- Make small changes rather than rewriting everything.
- Don't change unrelated code.
- Test changes after implementing them.
- Tell me exactly what you changed.
- Don't claim something works unless it was actually tested.
- Don't commit/push unless I ask.

## How we work together
- You write files and show them to me here. I copy them into the repo and
  commit myself. You do not push.
- I develop on native Windows with VS Code. `build.bat` is the Windows build;
  the `Makefile` is for CI and WSL.
- CI is the verification authority. If you cannot test something, say so
  explicitly and let the GitHub Actions run decide.

## What is built and verified

Verified on two independent toolchains: NASM 3.02 + QEMU 10.2 on Windows,
and NASM 2.16 + QEMU 8.2.2 on the Ubuntu CI runner.

- **Stage 1 bootloader** (`boot.asm`, 512 bytes). Loads stage 2 and the kernel
  sector by sector with LBA to CHS conversion, 3 retries and a controller
  reset between attempts. The sector count is supplied by the build, not
  hardcoded.
- **Stage 2** (`stage2.asm`, 2048 bytes). 16-bit real-mode shell: `help`,
  `clear`, `version`, `echo`, `sysinfo`, `pmode`. Enables the A20 gate, then
  switches to 32-bit protected mode through the GDT.
- **Kernel** (`kernel.asm`). CPUID vendor extraction, IDT load, PIC remap to
  vectors 32-47, PIT timer, `int 0` exception handler, timer ISR, VGA text
  output, and COM1 serial debug output.
- **Build system.** `build.bat` (Windows) and `Makefile` (Linux/WSL).
  `tools/mkimage.py` builds the disk image and **fails the build** if the
  kernel would be silently truncated by the bootloader's load window.
- **CI.** `.github/workflows/ci.yml` builds with `-DAUTOBOOT`, boots headless
  in QEMU, and asserts on 7 ordered serial markers ending in `BOOT_OK`.

## What is NOT verified

- The A20 fallback paths. QEMU's SeaBIOS supports `INT 15h AX=2401`, so that
  method always succeeds and the fast-A20 (port 0x92) and keyboard-controller
  paths have never executed. They are code-reviewed, not tested.
- Anything on physical hardware. Everything so far is QEMU only.

## Known bugs, deliberately deferred

1. **CPU model is wrong on modern CPUs.** The kernel reads CPUID EAX bits 4-7
   only and discards the extended model in bits 16-19. For family 6 the
   display model is `(ext_model << 4) + model`. Invisible on QEMU's default
   CPU (where ext_model is 0), wrong on real hardware.
2. **Exception handling is not real.** `int 0` is a software interrupt, not a
   genuine divide fault. No registers are saved, no error code is handled.
3. **`TIMER TICKS` value is misaligned** with its label on the VGA screen.
4. **Stage 2's protected-mode message is erased** by the kernel's
   `clear_screen`.

## The constraint that gates the next phase

The kernel has **309 bytes of headroom**. The bootloader reads CHS sectors
from track 0, which caps the load at roughly 17 sectors no matter how the
count is computed.

Phase 1 moves the kernel to C, which will exceed that limit almost
immediately. **The loader must be replaced before or alongside the C kernel**
- either extended across track boundaries, or replaced with an ELF loader
reading a real filesystem.

`tools/mkimage.py` will fail the build with an explicit message if this limit
is hit, so it cannot regress silently.

## Current task
Phase 1: move the kernel from assembly to C.

Before writing any C, resolve the loader limit above. Then set up an
`i686-elf` cross compiler (this is the point where WSL2 becomes worth the
setup cost, since building that toolchain natively on Windows is painful),
and establish a C entry point that the existing assembly can call into.

Keep the assembly for what genuinely needs it: the boot sectors, interrupt
service routine stubs, and later the context switch.