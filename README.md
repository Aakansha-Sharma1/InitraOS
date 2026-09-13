# InitraOS

A from-scratch operating system built to understand how an operating system works internally, starting from BIOS boot and gradually progressing toward a complete 64-bit OS.

InitraOS is developed step-by-step, with each feature implemented, tested, and tracked through GitHub Issues and Milestones.

---

## Current Status

**Current implementation:** Milestone 05 — Kernel Memory Management

**Next:** Milestone 06 — Process & Task Management

### Completed Milestones

- ✅ 01 — Boot & Hardware Initialization
- ✅ 02 — Kernel Foundation
- ✅ 03 — Interrupts & Hardware Handling
- ✅ 04 — Kernel Input & Shell
- ✅ 05 — Kernel Memory Management

### Upcoming

- ⬜ 06 — Process & Task Management
- ⬜ 07 — Process Isolation & Privilege
- ⬜ 08 — Virtual Memory & Paging
- ⬜ 09 — Physical Memory Manager
- ⬜ 10 — Transition to 64-bit Architecture
- ⬜ 11 — 64-bit Kernel Core
- ⬜ 12 — Advanced Memory Management
- ⬜ 13 — Process & User-Space Architecture
- ⬜ 14 — System Calls
- ⬜ 15 — Executable & User Program Support
- ⬜ 16 — Filesystem
- ⬜ 17 — Device & Hardware Abstraction
- ⬜ 18 — Networking
- ⬜ 19 — User Environment
- ⬜ 20 — Graphical Environment
- ⬜ 21 — OS Hardening & Reliability
- ⬜ 22 — Testing & CI
- ⬜ 23 — Final InitraOS Architecture

---

## What Has Been Implemented

### Boot & Hardware Initialization

- BIOS boot sector
- Two-stage bootloader
- Disk loading using INT 13h
- LBA disk reads
- CHS fallback
- A20 line enabling
- Global Descriptor Table (GDT)
- 32-bit Protected Mode
- Kernel handoff

### Kernel Foundation

- 32-bit kernel entry point
- VGA text output
- Serial debugging
- CPU identification using CPUID
- Kernel memory layout
- Kernel build and linking
- Kernel initialization sequence
- QEMU boot verification

### Interrupts & Hardware

- Interrupt Descriptor Table (IDT)
- CPU exception handling
- Software interrupts
- 8259 PIC initialization
- Hardware IRQ handling
- PIT timer initialization
- Timer interrupt testing

### Kernel Input & Shell

- PS/2 keyboard input
- Keyboard input buffer
- Backspace handling
- Enter / command-line input
- Shift and lowercase input
- Basic kernel command shell

Available shell commands include:

```text
help
clear
version
echo
sysinfo
pmode
