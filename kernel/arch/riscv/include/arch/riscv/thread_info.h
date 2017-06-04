/*
Some code has been borrowed from the Linux kernel which is under GPLv2 license.
2017 Modified for Magenta by Slava Imameev.
*/

#pragma once

#ifndef _ASM_OFFSETS_

#include <stdint.h>
#include <arch/riscv/asm/thread_info.h>

#endif

struct thread;

/*
 * low level task data that entry.S needs immediate access to
 * - this struct should fit entirely inside of one cache line
 * - this struct resides at the bottom of the supervisor stack
 * - if the members of this struct changes, the assembly constants
 *   in asm-offsets.c must be updated accordingly
 */
typedef struct thread_info {
    struct thread* 	    thread;		/* main task structure */
    unsigned long		flags;		/* low level flags */
    unsigned int        cpu;		/* current CPU */
} thread_info_t;

#ifndef _ASM_OFFSETS_

__BEGIN_CDECLS

union thread_union {
	thread_info_t thread_info;
	unsigned long stack[ARCH_DEFAULT_STACK_SIZE/sizeof(long)];
};

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/*
 * Pointer to the thread_info struct of the current process
 * Assumes that the kernel mode stack (thread_union) is THREAD_SIZE-aligned
 */
static inline thread_info_t *stack_to_thread_info(unsigned long sp)
{
    return (thread_info_t *)(sp & ~(ARCH_DEFAULT_STACK_SIZE - 1));
}

static inline thread_info_t *current_thread_info(void)
{
    register unsigned long sp;
     __asm__ __volatile__ (
         "move %0, sp"
         :"=r"(sp)
         ::);
    return stack_to_thread_info(sp);
}

__END_CDECLS

#endif // _ASM_OFFSETS_


