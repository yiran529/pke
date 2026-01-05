/*
 * Supervisor-mode startup codes
 */

#include "riscv.h"
#include "config.h"
#include "string.h"
#include "elf.h"
#include "process.h"

#include "spike_interface/spike_utils.h"

// process is a structure defined in kernel/process.h. We keep one per hart to avoid
// different harts overwriting the same process/trapframe metadata in multicore mode.
process user_app[NCPU];

//
// load the elf, and construct a "process" (with only a trapframe).
// load_bincode_from_host_elf is defined in elf.c
//
void load_user_program(process *proc) {
  // In multicore bare-metal mode, each hart must place its user stack/trapframe in a
  // non-overlapping region. We pick per-hart bases derived from config.h macros.
  int hartid = read_tp(); // mhartid cannot be read in S-mode; tp was set in m_start

  // USER_TRAP_FRAME is a physical address defined in kernel/config.h
  proc->trapframe = (trapframe *)USER_TRAPFRAME_BASE(hartid);
  memset(proc->trapframe, 0, sizeof(trapframe));
  // USER_KSTACK is also a physical address defined in kernel/config.h
  proc->kstack = USER_KSTACK_BASE(hartid); /// 每核独立内核栈，避免互相覆盖
  proc->trapframe->regs.sp = USER_STACK_BASE(hartid); /// 每核独立用户栈
  // Save hartid into tp slot so that when the trapframe is restored, tp reflects the
  // correct hart. This is required for per-hart logging in syscalls/interrupts because
  // tp is part of the saved context.
  proc->trapframe->regs.tp = hartid;

  // load_bincode_from_host_elf() is defined in kernel/elf.c
  load_bincode_from_host_elf(proc);
}

//
// s_start: S-mode entry point of riscv-pke OS kernel.
//
int s_start(void) {
  int hartid = read_tp();
  sprint("hartid = %d: Enter supervisor mode...\n", hartid);
  // Note: we use direct (i.e., Bare mode) for memory mapping in lab1.
  // which means: Virtual Address = Physical Address
  // therefore, we need to set satp to be 0 for now. we will enable paging in lab2_x.
  // 
  // write_csr is a macro defined in kernel/riscv.h
  write_csr(satp, 0);

  // the application code (elf) is first loaded into memory, and then put into execution
  process *p = &user_app[hartid];
  load_user_program(p);

  sprint("hartid = %d: Switch to user mode...\n", hartid);
  // switch_to() is defined in kernel/process.c
  switch_to(p);

  // we should never reach here.
  return 0;
}
