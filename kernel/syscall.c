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

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  // buf is now an address in user space of the given app's user stack,
  // so we have to transfer it into phisical address (kernel is running in direct mapping).
  assert( current );
  
  sprint("\n=== KERNEL MODE: Manual Translation Required ===\n");
  sprint("buf (user VA): 0x%lx\n", (uint64)buf);
  sprint("Current satp:  0x%lx (points to KERNEL page table)\n", read_csr(satp));
  sprint("User pagetable: 0x%lx\n", (uint64)current->pagetable);
  sprint("Why manual? satp != user_pagetable, so MMU can't auto-translate buf!\n");
  sprint("Calling user_va_to_pa() to manually walk user page table...\n");
  
  char* pa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)buf);
  sprint("Translated PA: 0x%lx\n", (uint64)pa);
  sprint("Message: %s", pa);
  sprint("===========================================\n\n");
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  sprint("User exit with code:%d.\n", code);
  // in lab1, PKE considers only one app (one process). 
  // therefore, shutdown the system when the app calls exit()
  shutdown(code);
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
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
