# InitraOS 64-bit Kernel ABI

## Purpose

This document defines the calling and execution conventions used between
InitraOS 64-bit assembly and 64-bit C code.

The ABI is based on the System V AMD64 calling convention, with kernel-specific
rules where required.

---

## 1. Execution Mode

The 64-bit kernel executes in:

- x86-64 long mode
- Ring 0
- 64-bit code segment
- 64-bit data segment

The 64-bit C kernel is entered through the assembly bootstrap:

    kernel64_entry
        ↓
    kernel64_c_main
        ↓
    kernel64_main()

---

## 2. General-Purpose Registers

The 64-bit kernel uses the standard System V AMD64 register convention.

### Function arguments

Arguments are passed in this order:

| Argument | Register |
|---|---|
| 1st | RDI |
| 2nd | RSI |
| 3rd | RDX |
| 4th | RCX |
| 5th | R8 |
| 6th | R9 |

Additional arguments, if ever required, are passed on the stack according to
the System V AMD64 convention.

### Return value

The primary return value is returned in:

    RAX

---

## 3. Caller-Saved Registers

A C function may freely modify:

    RAX
    RCX
    RDX
    RSI
    RDI
    R8
    R9
    R10
    R11

Assembly code calling a C function must preserve these values itself if it
needs them after the call.

---

## 4. Callee-Saved Registers

A C function must preserve:

    RBX
    RBP
    R12
    R13
    R14
    R15

If assembly code calls a C function, these registers may be relied upon to
retain their values across the call.

---

## 5. Stack Alignment

The stack pointer must satisfy:

    RSP % 16 == 0

immediately before executing a `CALL` instruction.

`CALL` pushes an 8-byte return address.

Therefore, on entry to the called C function:

    RSP % 16 == 8

The current 64-bit kernel entry follows this rule.

---

## 6. Red Zone

The 128-byte System V red zone is disabled for the kernel.

The 64-bit C compiler is therefore invoked with:

    -mno-red-zone

Kernel code must not depend on memory below RSP remaining untouched across
interrupts or other asynchronous kernel activity.

---

## 7. Pointer and Address Width

64-bit kernel code uses 64-bit addresses.

Pointers used by 64-bit C code therefore have the native x86-64 pointer width.

When communicating with existing 32-bit kernel code, addresses must not be
silently truncated from 64 bits to 32 bits.

Such boundaries require an explicitly defined conversion.

---

## 8. Integer Width

The 64-bit kernel follows the normal x86-64 C data model used by the compiler.

Fixed-width integer types should be preferred when the exact size matters:

    uint8_t
    uint16_t
    uint32_t
    uint64_t

Pointer-sized values should use an appropriate pointer-width type rather than
assuming that an address fits in `uint32_t`.

---

## 9. Assembly-to-C Entry

The current 64-bit C entry is intentionally minimal.

Assembly establishes the kernel stack and then calls the C entry point:

    kernel64_entry
        ↓
    establish RSP
        ↓
    call kernel64_c_main
        ↓
    kernel64_main()

The C entry must return normally during the current validation stage.

---

## 10. Interrupt Boundary

Hardware interrupts and CPU exceptions remain assembly-owned.

64-bit interrupt handlers:

- establish the required register state
- perform low-level hardware operations
- use `IRETQ` to return from the interrupt

C interrupt handlers must not be introduced until an explicit interrupt-frame
ABI has been defined.

---

## 11. Syscall Boundary

The existing syscall interface is currently a 32-bit interface.

The 64-bit syscall ABI is not yet defined.

It will be specified separately before migrating the syscall subsystem to
64-bit C.

---

## 12. Context-Switch Boundary

The existing task context structures are currently 32-bit.

They must not be reused as 64-bit structures without explicitly defining:

- 64-bit register storage
- instruction pointer width
- stack pointer width
- flags width
- privilege information
- interrupt/context frame layout

Context-switch migration is therefore deferred until the 64-bit context ABI
is defined.

---

## 13. Current ABI Boundary

At the current migration stage:

    32-bit kernel C
            │
            │ existing 32-bit ABI
            ▼
    32-bit assembly
            │
            │ long-mode transition
            ▼
    64-bit assembly
            │
            │ System V AMD64 ABI
            ▼
    64-bit C

The two ABIs must be treated as separate interfaces.

---

## 14. Migration Rule

New 64-bit C code must follow this ABI.

Existing 32-bit C code must not be converted implicitly.

Each subsystem migrated to 64-bit must explicitly define and validate its
64-bit interface before its implementation is changed.

---

## 15. Current Validation

The current ABI boundary has been runtime-tested.

The boot test validates:

    KERNEL64_ENTRY_OK
    KERNEL64_C_OK

and the complete boot sequence currently passes:

    PASS: all 49 boot markers present and in order