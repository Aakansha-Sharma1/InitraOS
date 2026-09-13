# InitraOS Development Log

This document records the development of InitraOS from its initial boot process toward a complete 64-bit operating system.

The project is developed incrementally, with each feature implemented, tested, and tracked through GitHub Issues and Milestones.

---

## Milestone 01 — Boot & Hardware Initialization

### Completed

- Implemented BIOS boot sector
- Implemented Stage 2 bootloader
- Implemented disk loading
- Added INT 13h LBA disk reads
- Added CHS fallback
- Enabled A20 line
- Implemented GDT
- Entered 32-bit Protected Mode
- Transferred control to the kernel

---

## Milestone 02 — Kernel Foundation

### Completed

- Implemented 32-bit kernel entry point
- Added VGA text output
- Added serial debugging
- Added CPU information using CPUID
- Established kernel memory layout
- Integrated kernel build and linking
- Established kernel initialization sequence
- Added boot verification

---

## Milestone 03 — Interrupts & Hardware Handling

### Completed

- Implemented IDT
- Implemented CPU exception handling
- Implemented software interrupt handling
- Implemented 8259 PIC initialization
- Implemented hardware IRQ handling
- Implemented PIT timer initialization
- Validated interrupt and timer integration

---

## Milestone 04 — Kernel Input & Shell

### Completed

- Implemented keyboard input
- Implemented keyboard input buffer
- Added Backspace handling
- Added Enter and command-line input
- Added Shift and lowercase input
- Implemented basic kernel command shell

---

## Milestone 05 — Kernel Memory Management

### Completed

- Established kernel heap region
- Implemented basic heap allocator
- Added heap block metadata
- Added free-block reuse
- Implemented safe heap freeing
- Implemented heap block splitting
- Implemented two-sided heap coalescing
- Added heap bounds protection
- Added allocation-size alignment
- Added double-free protection
- Implemented `realloc`
- Validated heap integrity

### Validation

Heap functionality was tested for:

- Allocation
- Freeing
- Block reuse
- Splitting
- Coalescing
- Bounds protection
- Alignment
- Double-free protection
- Reallocation
- Final heap integrity

---

## Next Milestone

### Milestone 06 — Process & Task Management

The next stage is to introduce the first task-related kernel structures, beginning with the task control structure.

Planned progression:

```text
Task Structure
      ↓
CPU Context
      ↓
Task Creation
      ↓
Task States
      ↓
Task Switching
      ↓
Scheduler
      ↓
Timer-driven Scheduling
````

---

## Long-Term Goal

Build a complete 64-bit operating system from the ground up, progressing from the current 32-bit kernel toward:

* Process isolation
* Virtual memory
* Physical memory management
* 64-bit architecture
* User-space programs
* System calls
* Filesystem support
* Networking
* Graphical environment
* Desktop applications

---

## Development Approach

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
Next feature
```

Each milestone is maintained through GitHub Issues, Milestones, Projects, commits, and automated testing.
Once that's done, tell me **“done”** and we'll take the next small step.
```
