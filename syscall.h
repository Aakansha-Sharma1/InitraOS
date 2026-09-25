#ifndef INITRAOS_SYSCALL_H
#define INITRAOS_SYSCALL_H

/*
 * InitraOS system call interface
 *
 * User-space system calls use INT 0x80.
 *
 * Register convention:
 *   EAX = system call number
 *   EBX = argument 1
 *   ECX = argument 2
 *   EDX = argument 3
 *   ESI = argument 4
 *   EDI = argument 5
 *
 * Return value:
 *   EAX = system call result
 */

#define SYSCALL_VECTOR 0x80

#define SYSCALL_EXIT   0
#define SYSCALL_WRITE  1
#define SYSCALL_GETPID 2
#define SYSCALL_YIELD  3
#define SYSCALL_ALLOC  4
#define SYSCALL_FREE   5
#define SYSCALL_SECURITY_CHECK   6

#endif