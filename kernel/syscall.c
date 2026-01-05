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
#include "config.h"

#include "spike_interface/spike_utils.h"

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  int hid = read_tp();
  // Add hartid to prints so we can distinguish which hart produced the user message.
  sprint("hartid = %d: %s\n", hid, buf);
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
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
    sprint("hartid = %d: shutdown with code:%d.\n", hid, code);
    shutdown(code);
  }

  // After reporting exit, disable further timer interrupts on this hart to avoid being
  // re-entered while parked. Clear S-mode enables and pending bits (accessible in S).
  write_csr(sie, 0);
  write_csr(sip, 0);

  // Non-zero harts just park; hart0 will eventually power off when counter reaches
  // NCPU.
  /*
  作用：进入低功耗/空转状态，同时保持在 S 态；不会继续执行后续代码，防止已退出的 hart 触碰无效指针或重复触发 trap。
  多核理由：只有 hart0 在所有 hart 退出后调用 shutdown。其他 hart 先退出时必须留在安全的停机位置，等待 hart0 关机；
    用 WFI 比死循环空跑更符合架构习惯，且可响应需要时的外部中断（本实验关了 sie/sip，不会再被唤醒）。
  安全性：在它之前我们已关 sie/sip，所以不会被 S 态定时器等再次打断，确保 hart 安全停住。
   */
  while (1) asm volatile("wfi");
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
