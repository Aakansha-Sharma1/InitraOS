# InitraOS

A from-scratch operating system built to understand how an operating system works internally, starting from BIOS boot and gradually progressing toward a complete 64-bit OS.

InitraOS is developed step-by-step, with each feature implemented, tested, and tracked through GitHub Issues and Milestones.

---

## 🚀 Current Status

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
````

### Kernel Memory Management

* Kernel heap region
* Heap block metadata
* Dynamic heap allocation
* Free-block reuse
* Safe memory freeing
* Heap block splitting
* Two-sided heap coalescing
* Heap bounds protection
* Allocation-size alignment
* Double-free protection
* `realloc` support
* Heap integrity validation

---

## Architecture

```text
BIOS
 │
 ▼
Boot Sector
 │
 ├── INT 13h disk loading
 ├── LBA support
 └── CHS fallback
 │
 ▼
Stage 2 Bootloader
 │
 ├── A20
 ├── GDT
 └── Protected Mode
 │
 ▼
32-bit Kernel
 │
 ├── VGA
 ├── Serial Debugging
 ├── IDT
 ├── CPU Exceptions
 ├── PIC / IRQ
 ├── PIT Timer
 ├── Keyboard
 ├── Shell
 └── Kernel Heap
 │
 ▼
Process & Task Management
        │
        ▼
   User Space
        │
        ▼
 System Calls
        │
        ▼
 Filesystem
        │
        ▼
 Graphical Environment
```

The architecture will evolve as new kernel and user-space components are implemented.

---

## Memory Layout

```text
0x0000 ─────────────────────
        BIOS / Low Memory

0x7C00
        Bootloader
        │
0x8000
        Stage 2
        │
0x8800
        Kernel
        │
        Kernel Heap
        │
0x80000 ────────────────────
        Current load ceiling
```

The memory layout will evolve as paging, physical memory management, and 64-bit support are introduced.

---

## Technologies

* **C**
* **x86 Assembly**
* **NASM**
* **GCC**
* **GNU Make**
* **QEMU**
* **Git & GitHub**
* **GitHub Actions**

---

## Building

### Linux / WSL

Install the required tools:

```bash
sudo apt install gcc make nasm qemu-system-x86
```

Build:

```bash
make
```

Run in QEMU:

```bash
make run
```

Run tests:

```bash
make test
```

Clean build files:

```bash
make clean
```

### Windows

The project also provides:

```text
build.bat
```

The Windows build uses NASM, Python, and QEMU.

---

## Testing

InitraOS is tested primarily through QEMU.

The project also uses GitHub Actions to automatically build and boot-test the operating system.

CI verifies that the kernel reaches the expected boot state before a change is considered successful.

---

## Project Structure

```text
InitraOS/
│
├── .github/
│   └── workflows/
│
├── tools/
│   └── mkimage.py
│
├── boot.asm
├── stage2.asm
├── kernel.asm
├── kernel.c
│
├── gdt.inc
├── a20.inc
├── idt.inc
├── serial.inc
│
├── linker.ld
├── Makefile
├── build.bat
├── OS_DEV_STATE.md
└── README.md
```

---

## Long-Term Goal

The long-term goal of InitraOS is to build a complete modern operating system from the ground up.

The project will progressively move from the current 32-bit kernel toward:

* Multitasking
* Process isolation
* Virtual memory
* Physical memory management
* 64-bit architecture
* User-space programs
* System calls
* Filesystem support
* Networking
* User environment
* Graphical desktop environment

The focus is not only on making the OS work, but on understanding how each layer is implemented.

---

## Development Philosophy

InitraOS follows a simple development approach:

```text
Plan
 ↓
Implement
 ↓
Test
 ↓
Document
 ↓
Commit
 ↓
Move to the next feature
```

Every major feature is tracked through GitHub Issues and Milestones to maintain a clear development history.

---

## License

This project is currently developed as a personal learning and systems-programming project.

```
