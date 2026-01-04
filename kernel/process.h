#ifndef _PROC_H_
#define _PROC_H_

#include "riscv.h"
#include "config.h"

typedef struct trapframe_t {
  // space to store context (all common registers)
  /* offset:0   */ riscv_regs regs; /// 保存所有通用寄存器的值，包括ra, sp, gp, tp, t0-t6, a0-a7, s0-s11等

  // process's "user kernel" stack
  /* offset:248 */ uint64 kernel_sp; /// 指向进程的内核栈，用于trap处理时的栈空间
  // pointer to smode_trap_handler
  /* offset:256 */ uint64 kernel_trap; /// 指向 smode_trap_handler，用于 trap 处理
  // saved user process counter
  /* offset:264 */ uint64 epc; /// 保存用户程序的程序计数器（Exception Program Counter），用于返回用户态时恢复执行
}trapframe;

// the extremely simple definition of process, used for begining labs of PKE
typedef struct process_t {
  // pointing to the stack used in trap handling.
  uint64 kstack;
  // trapframe storing the context of a (User mode) process.
  trapframe* trapframe;
}process;

void switch_to(process*);

// Each hart keeps its own current pointer to avoid clobbering another hart's context.
// In multicore, timer/syscall traps must return to the process that was running on that
// hart, so we store per-hart 'current'.
extern process* current[NCPU];

#endif
