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
#include "elf.h"

#include "spike_interface/spike_utils.h"

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  sprint(buf);
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

ssize_t sys_user_print_backtrace(uint32 nlayers) {
  // 重新打开 ELF 文件
  arg_buf arg_bug_msg;
  size_t argc = parse_args(&arg_bug_msg);  // 需要将 parse_args 改为非 static

  elf_ctx elfloader;
  elf_info info;
  
  info.f = spike_file_open(arg_bug_msg.argv[0], O_RDONLY, 0);
  info.p = current;
  
  if (IS_ERR_VALUE(info.f)) {
    sprint("Failed to open ELF file\n");
    return -1;
  }
  
  if (elf_init(&elfloader, &info) != EL_OK) {
    spike_file_close(info.f);
    return -1;
  }

  uint64 fp = current->trapframe->regs.s0; // fp寄存器
  uint64 ra = current->trapframe->regs.ra; 
  elf_section_header section_headers[20]; //理论上限远不止20，这里为了方便而进行简化
 
  // sprint("[DEBUG] ra: 0x%lx\n", ra);
  // sprint("[DEBUG] fp: 0x%lx\n", fp);
  // sprint("[DEBUG] (fp-8) - 0 addr: 0x%lx, val: 0x%lx\n", (uint64)((uint64*)(fp - 8)-0), *((uint64*)(fp - 8)-0));
  // sprint("[DEBUG] (fp-8) + 1 addr: 0x%lx, val: 0x%lx\n", (uint64)((uint64*)(fp - 8)+1), *((uint64*)(fp - 8)+1));
  // sprint("[DEBUG] (fp-8) + 2 addr: 0x%lx, val: 0x%lx\n", (uint64)((uint64*)(fp - 8)+2), *((uint64*)(fp - 8)+2));
  // sprint("[DEBUG] (fp-8) + 3 addr: 0x%lx, val: 0x%lx\n", (uint64)((uint64*)(fp - 8)+3), *((uint64*)(fp - 8)+3));
  // sprint("[DEBUG] (fp-8) + 4 addr: 0x%lx, val: 0x%lx\n", (uint64)((uint64*)(fp - 8)+4), *((uint64*)(fp - 8)+4));
  // sprint("[DEBUG] (fp-8) + 5 addr: 0x%lx, val: 0x%lx\n", (uint64)((uint64*)(fp - 8)+5), *((uint64*)(fp - 8)+5));
  // sprint("[DEBUG] (fp-8) + 6 addr: 0x%lx, val: 0x%lx\n", (uint64)((uint64*)(fp - 8)+6), *((uint64*)(fp - 8)+6));

  // 不需要打印出print_backtrace本身的信息
  uint64* st_ptr = (uint64*)(*(uint64*)(fp - 8));
  uint64 prev_fp = (uint64)(st_ptr - 1);
  uint64 prev_ra = *(st_ptr - 1);
  fp = prev_fp;
  ra = prev_ra;

  while(get_name_by_ra(&elfloader, section_headers, ra)) {
     uint64* st_ptr = (uint64*)(*(uint64*)(fp - 8));
     uint64 prev_fp = (uint64)(st_ptr - 1);
     uint64 prev_ra = *(st_ptr - 1);
     fp = prev_fp;
     ra = prev_ra;
    //  sprint("[DEBUG] st_ptr addr: 0x%lx, prev_ra: 0x%lx, prev_fp: 0x%lx\n", (uint64)st_ptr, ra, fp);
     nlayers--;
     if (nlayers == 0) break;
  }
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
    case SYS_user_print_backtrace:
      return sys_user_print_backtrace(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
