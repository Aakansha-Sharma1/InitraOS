; InitraOS - 64-bit Page Table Structures
;
; Issue #78:
; Define the physical memory layout required for
; x86-64 four-level paging.
;
; The tables are reserved outside stage2.bin so the
; existing 2048-byte Stage 2 limit is preserved.
;
; Table initialization and mapping are implemented
; in later issues.

bits 32

; ---------------------------------------------------------
; x86-64 paging flags
; ---------------------------------------------------------

%define PAGE64_PRESENT  0x001
%define PAGE64_WRITABLE 0x002

; ---------------------------------------------------------
; x86-64 paging structure
;
; PML4
;   |
;   +--> PDPT
;          |
;          +--> Page Directory
;                   |
;                   +--> Page Table
;
; Each table:
;   512 entries
;   8 bytes per entry
;   4096 bytes
; ---------------------------------------------------------

%define PAGE64_ENTRIES       512
%define PAGE64_ENTRY_SIZE    8
%define PAGE64_TABLE_SIZE    4096

; ---------------------------------------------------------
; Reserved physical memory for the four tables.
;
; These addresses are page-aligned and remain outside:
;   Stage 2 : 0x00008000
;   Kernel  : 0x00008800
;
; Total reserved space:
;   4 x 4096 = 16384 bytes
; ---------------------------------------------------------

%define PML4_TABLE_ADDRESS        0x00010000
%define PDPT_TABLE_ADDRESS        0x00011000
%define PAGE_DIRECTORY64_ADDRESS  0x00012000
%define PAGE_TABLE64_ADDRESS      0x00013000

; ---------------------------------------------------------
; End of 64-bit paging structure definitions.
;
; The memory is intentionally not populated here.
; ---------------------------------------------------------