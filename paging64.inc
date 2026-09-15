; InitraOS - 64-bit Page Table Structures
;
; Issue #79 / #80:
; Build the initial identity mapping required for
; the future x86-64 long-mode transition.
;
; The mapping covers the first 16 MiB so the existing
; kernel stack and transition execution state remain mapped
; when paging is re-enabled for long mode.

bits 32

%define PAGE64_PRESENT   0x001
%define PAGE64_WRITABLE  0x002
%define PAGE64_FLAGS     (PAGE64_PRESENT | PAGE64_WRITABLE)

%define PAGE64_ENTRIES       512
%define PAGE64_ENTRY_SIZE    8
%define PAGE64_TABLE_SIZE    4096

%define PML4_TABLE_ADDRESS        0x00020000
%define PDPT_TABLE_ADDRESS        0x00021000
%define PAGE_DIRECTORY64_ADDRESS  0x00022000
%define PAGE_TABLE64_ADDRESS      0x00023000

%define PAGE_TABLE64_COUNT        8

paging64_init:

    ; Clear PML4.
    mov edi, PML4_TABLE_ADDRESS
    xor eax, eax
    mov ecx, 4096
    rep stosd

    ; Clear PDPT.
    mov edi, PDPT_TABLE_ADDRESS
    xor eax, eax
    mov ecx, 4096
    rep stosd

    ; Clear page directory.
    mov edi, PAGE_DIRECTORY64_ADDRESS
    xor eax, eax
    mov ecx, 4096
    rep stosd

    ; Clear all 8 page tables.
    mov edi, PAGE_TABLE64_ADDRESS
    xor eax, eax
    mov ecx, (PAGE64_TABLE_SIZE * PAGE_TABLE64_COUNT) / 4
    rep stosd

    ; PML4[0] -> PDPT.
    mov dword [PML4_TABLE_ADDRESS], \
        PDPT_TABLE_ADDRESS | PAGE64_FLAGS
    mov dword [PML4_TABLE_ADDRESS + 4], 0

    ; PDPT[0] -> Page Directory.
    mov dword [PDPT_TABLE_ADDRESS], \
        PAGE_DIRECTORY64_ADDRESS | PAGE64_FLAGS
    mov dword [PDPT_TABLE_ADDRESS + 4], 0

    ; Connect 8 page-directory entries to 8 page tables.
    mov edi, PAGE_DIRECTORY64_ADDRESS
    mov eax, PAGE_TABLE64_ADDRESS
    mov ecx, PAGE_TABLE64_COUNT

.fill_page_directories:

    mov edx, eax
    or edx, PAGE64_FLAGS

    mov dword [edi], edx
    mov dword [edi + 4], 0

    add eax, PAGE64_TABLE_SIZE
    add edi, PAGE64_ENTRY_SIZE

    loop .fill_page_directories

    ; Fill 4096 page-table entries.
    ; 4096 × 4 KiB = 16 MiB identity mapping.
    mov edi, PAGE_TABLE64_ADDRESS
    xor eax, eax
    mov ecx, PAGE64_ENTRIES * PAGE_TABLE64_COUNT

.fill_page_tables:

    mov edx, eax
    or edx, PAGE64_FLAGS

    mov dword [edi], edx
    mov dword [edi + 4], 0

    add eax, 0x1000
    add edi, PAGE64_ENTRY_SIZE

    loop .fill_page_tables

    ret