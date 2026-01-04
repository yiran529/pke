#ifndef _CONFIG_H_
#define _CONFIG_H_

// we use two HART (cpu) in challenge3
#define NCPU 2

//interval of timer interrupt. added @lab1_3
#define TIMER_INTERVAL 1000000

#define DRAM_BASE 0x80000000

/* we use fixed physical (also logical) addresses for the stacks and trap frames as in
 Bare memory-mapping mode */
// Per-hart user memory layout. In bare mode the two apps share the same physical
// address space, so we must keep their user stacks/trapframes/kernel-stacks disjoint
// to avoid mutual overwrite. hart0 keeps原地址，hart1整体平移 0x04000000。
//
// This separation is required only because we do not have paging/isolation yet in
// lab1; once paging exists, each hart/process would have its own page table.
#define HART1_OFFSET        0x04000000

#define USER_STACK_BASE(h)     ((h) == 0 ? 0x81100000UL : 0x81100000UL + HART1_OFFSET)
#define USER_KSTACK_BASE(h)    ((h) == 0 ? 0x81200000UL : 0x81200000UL + HART1_OFFSET)
#define USER_TRAPFRAME_BASE(h) ((h) == 0 ? 0x81300000UL : 0x81300000UL + HART1_OFFSET)

#endif
