; InitraOS - 64-bit Page Table Structures
;
; Issue #79:
; Implement the initial identity mapping required
; for the future x86-64 long-mode transition.
;
; The tables are located outside stage2.bin so the
; existing 2048-byte Stage 2 limit is preserved.

bits 32

; ---------------------------------------------------------
; x86-64 paging flags
; ---------------------------------------------------------

%define PAGE64_PRESENT   0x001
%define PAGE64_WRITABLE  0x002
%define PAGE64_FLAGS     (PAGE64_PRESENT | PAGE64_WRITABLE)

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
;   4096 bytes per table
; ---------------------------------------------------------

%define PAGE64_ENTRIES       512
%define PAGE64_ENTRY_SIZE    8
%define PAGE64_TABLE_SIZE    4096

; ---------------------------------------------------------
; Physical addresses of the four tables
; ---------------------------------------------------------

%define PML4_TABLE_ADDRESS        0x00010000
%define PDPT_TABLE_ADDRESS        0x00011000
%define PAGE_DIRECTORY64_ADDRESS  0x00012000
%define PAGE_TABLE64_ADDRESS      0x00013000

; ---------------------------------------------------------
; paging64_init
;
; Build an initial identity mapping for the first 2 MiB.
;
; Mapping:
;
;   Virtual 0x00000000 -> Physical 0x00000000
;   through
;   Virtual 0x001FFFFF -> Physical 0x001FFFFF
;
; This creates:
;
;   PML4[0] -> PDPT
;   PDPT[0] -> Page Directory
;   PD[0] -> Page Table
;   PT[0..511] -> 4 KiB identity-mapped pages
;
; Long mode and paging are NOT enabled here.
; ---------------------------------------------------------

paging64_init:

    ; Clear all four 4 KiB tables.
    mov edi, PML4_TABLE_ADDRESS
    xor eax, eax
    mov ecx, 4096
    rep stosd

    ; PML4[0] -> PDPT
    mov dword [PML4_TABLE_ADDRESS], PDPT_TABLE_ADDRESS | PAGE64_FLAGS
    mov dword [PML4_TABLE_ADDRESS + 4], 0

    ; PDPT[0] -> Page Directory
    mov dword [PDPT_TABLE_ADDRESS], PAGE_DIRECTORY64_ADDRESS | PAGE64_FLAGS
    mov dword [PDPT_TABLE_ADDRESS + 4], 0

    ; Page Directory[0] -> Page Table
    mov dword [PAGE_DIRECTORY64_ADDRESS], PAGE_TABLE64_ADDRESS | PAGE64_FLAGS
    mov dword [PAGE_DIRECTORY64_ADDRESS + 4], 0

    ; Fill 512 Page Table entries.
    mov edi, PAGE_TABLE64_ADDRESS
    xor eax, eax
    mov ecx, PAGE64_ENTRIES

.fill_page_table:
    mov edx, eax
    or edx, PAGE64_FLAGS

    mov dword [edi], edx
    mov dword [edi + 4], 0

    add eax, 0x1000
    add edi, PAGE64_ENTRY_SIZE
    loop .fill_page_table

    ret