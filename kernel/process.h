#ifndef _PROC_H_
#define _PROC_H_

#include "riscv.h"

typedef struct trapframe_t {
  // space to store context (all common registers)
  /* offset:0   */ riscv_regs regs; /// 保存所有通用寄存器的值，包括ra, sp, gp, tp, t0-t6, a0-a7, s0-s11等

  // process's "user kernel" stack
  /* offset:248 */ uint64 kernel_sp; /// 指向进程的内核栈，用于trap处理时的栈空间
  // pointer to smode_trap_handler
  /* offset:256 */ uint64 kernel_trap; /// 指向 smode_trap_handler，用于 trap 处理
  // saved user process counter
  /* offset:264 */ uint64 epc;

  // kernel page table. added @lab2_1
  /* offset:272 */ uint64 kernel_satp;
}trapframe;

// the extremely simple definition of process, used for begining labs of PKE
typedef struct process_t {
  // pointing to the stack used in trap handling.
  uint64 kstack;
  // user page table
  pagetable_t pagetable;
  // trapframe storing the context of a (User mode) process.
  trapframe* trapframe;
  // heap base pa 
  uint64 heap_pa;
  // heap base va
  uint64 heap_va;
  // current heap size in bytes
  uint64 heap_size;
}process;

// switch to run user app
void switch_to(process*);

// current running process
extern process* current;

// address of the first free page in our simple heap. added @lab2_2
extern uint64 g_ufree_page;

/* Below are macro, data structures, or functions for better_malloc or better_free */
#define CHUNK_ALIGN 16
#define CHUNK_HDR_SIZE 16
#define CHUNK_MIN_SIZE (CHUNK_HDR_SIZE + CHUNK_ALIGN)
#define CHUNK_IS_FREE(c) ((c)->flags == 0)
#define ALIGN_UP(size, align) (((size) + (align)-1) & ~((align)-1))

typedef struct heap_chunk {
  uint32 size; // 含头部的总字节数
  uint32 prev_size; // 上一块总字节数，便于 O(1) 向前合并
  uint8 flags; // 0-free 1-used
  char reserverd[7]; // 对齐填充
}heap_chunk_t; // total: 16 bytes

#endif
