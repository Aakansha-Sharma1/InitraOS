#ifndef INITRAOS_PROGRAM_H
#define INITRAOS_PROGRAM_H

/*
 * InitraOS user program format
 *
 * Layout:
 *
 *   +----------------------+
 *   | Program header       |
 *   +----------------------+
 *   | Code                 |
 *   +----------------------+
 *   | Initialized data     |
 *   +----------------------+
 *
 * BSS is not stored in the file.
 * The loader reserves and clears it in memory.
 */

#define INITRAOS_PROGRAM_MAGIC   0x49504F53U
#define INITRAOS_PROGRAM_VERSION 1U

/*
 * Program entry point is a virtual address
 * relative to the program's user image.
 */
typedef struct
{
    unsigned int magic;
    unsigned int version;
    unsigned int entry;
    unsigned int code_size;
    unsigned int data_size;
    unsigned int bss_size;
} initraos_program_header_t;

#endif
