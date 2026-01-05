#ifndef _CONFIG_H_
#define _CONFIG_H_

// we use two HART (cpu) in challenge3
#define NCPU 2

//interval of timer interrupt. added @lab1_3
#define TIMER_INTERVAL 1000000

// the maximum memory space that PKE is allowed to manage. added @lab2_1
#define PKE_MAX_ALLOWABLE_RAM 128 * 1024 * 1024

// the ending physical address that PKE observes. added @lab2_1
#define PHYS_TOP (DRAM_BASE + PKE_MAX_ALLOWABLE_RAM)

#define HART1_OFFSET        0x04000000

// #define USER_STACK_BASE(h)     ((h) == 0 ? 0x81100000UL : 0x81100000UL + HART1_OFFSET)
// #define USER_KSTACK_BASE(h)    ((h) == 0 ? 0x81200000UL : 0x81200000UL + HART1_OFFSET)
// #define USER_TRAPFRAME_BASE(h) ((h) == 0 ? 0x81300000UL : 0x81300000UL + HART1_OFFSET)

#endif
