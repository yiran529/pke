/*
 * contains the implementation of all syscalls.
 */

#include <stdint.h>
#include <errno.h>

#include "util/types.h"
#include "syscall.h"
#include "string.h"
#include "process.h"
#include "util/functions.h"
#include "pmm.h"
#include "vmm.h"
#include "spike_interface/spike_utils.h"
#include "config.h"

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  // buf is now an address in user space of the given app's user stack,
  // so we have to transfer it into phisical address (kernel is running in direct mapping).
  int hid = read_tp();
  process* p = current[hid];

  assert(p);
  
  // sprint("\n=== KERNEL MODE: Manual Translation Required ===\n");
  // sprint("buf (user VA): 0x%lx\n", (uint64)buf);
  // sprint("Current satp:  0x%lx (points to KERNEL page table)\n", read_csr(satp));
  // sprint("User pagetable: 0x%lx\n", (uint64)current->pagetable);
  // sprint("Why manual? satp != user_pagetable, so MMU can't auto-translate buf!\n");
  // sprint("Calling user_va_to_pa() to manually walk user page table...\n");
  
  char* pa = (char*)user_va_to_pa((pagetable_t)(p->pagetable), (void*)buf);
  // sprint(pa);
  // Add hartid to prints so we can distinguish which hart produced the user message.
  sprint("hartid = %d: %s\n", hid, pa);
  // sprint("Translated PA: 0x%lx\n", (uint64)pa);
  // sprint("Message: %s", pa);
  // sprint("===========================================\n\n");
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  // sprint("hartid = ?: User exit with code:%d.\n", code);
  // // in lab1, PKE considers only one app (one process). 
  // // therefore, shutdown the system when the app calls exit()
  // sprint("hartid = ?: shutdown with code:%d.\n", code);
  // shutdown(code);
  int hid = read_tp();
  sprint("hartid = %d: User exit with code:%d.\n", hid, code);

  // Cooperative shutdown: in multicore we must wait until all harts finish before
  // calling shutdown, otherwise one hart would terminate others prematurely.
  static volatile int exit_count = 0;

  // atomic add: every hart that exits increments the counter. Using amoor to avoid
  // needing a lock in this simple setting.
  int old;
  asm volatile("amoadd.w %0, %1, (%2)"
               : "=r"(old)
               : "r"(1), "r"(&exit_count)
               : "memory");

  int newval = old + 1;
  if (hid == 0) {
    // hart0 waits until all harts report exit, then shuts down the system.
    while (newval < NCPU) {
      newval = exit_count; // busy-wait; simple and sufficient for this lab
    }
    sprint("hartid = %d: shutdown with code:%d.\\n", hid, code);
    shutdown(code);
  }

  // After reporting exit, disable further timer interrupts on this hart to avoid being
  // re-entered while parked. Clear S-mode enables and pending bits (accessible in S).
  write_csr(sie, 0);
  write_csr(sip, 0);

  // Non-zero harts just park; hart0 will eventually power off when counter reaches
  // NCPU.
  while (1) asm volatile("wfi");
}

//
// maybe, the simplest implementation of malloc in the world ... added @lab2_2
//
uint64 sys_user_allocate_page() {
  int hid = read_tp();
  process* p = current[hid];

  void* pa = alloc_page();
  /*取当前进程“用户简单堆”指针的当前位置作为本次分配的虚拟页起始地址。
  ufree_page 是每个进程独立的单调递增游标，表示该进程下一个可用的用户虚拟地址*/
  uint64 va = p->ufree_page;
  p->ufree_page += PGSIZE;
  user_vm_map((pagetable_t)p->pagetable, va, PGSIZE, (uint64)pa,
         prot_to_type(PROT_WRITE | PROT_READ, 1));
  sprint("hartid = %d: vaddr 0x%x is mapped to paddr 0x%x\n", hid, va, pa);
  return va;
}

//
// reclaim a page, indicated by "va". added @lab2_2
//
uint64 sys_user_free_page(uint64 va) {
  int hid = read_tp();
  process* p = current[hid];

  user_vm_unmap((pagetable_t)p->pagetable, va, PGSIZE, 1);
  return 0;
}

//
// [a0]: the syscall number; [a1] ... [a7]: arguments to the syscalls.
// returns the code of success, (e.g., 0 means success, fail for otherwise)
//
long do_syscall(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
  switch (a0) {
    case SYS_user_print:
      return sys_user_print((const char*)a1, a2);
    case SYS_user_exit:
      return sys_user_exit(a1);
    // added @lab2_2
    case SYS_user_allocate_page:
      return sys_user_allocate_page();
    case SYS_user_free_page:
      return sys_user_free_page(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
