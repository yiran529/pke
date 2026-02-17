#include "kernel/riscv.h"
#include "kernel/process.h"
#include "spike_interface/spike_utils.h"
#include "util/string.h"

static void handle_instruction_access_fault() { panic("Instruction access fault!"); }

static void handle_load_access_fault() { panic("Load access fault!"); }

static void handle_store_access_fault() { panic("Store/AMO access fault!"); }

static void handle_illegal_instruction() { panic("Illegal instruction!"); }

static void handle_misaligned_load() { panic("Misaligned Load!"); }

static void handle_misaligned_store() { panic("Misaligned AMO!"); }

// added @lab1_3
static void handle_timer() {
  /*handle_timer()函数会（在~21行）先设置下一次timer（再次）触发的时间为当前时间+TIMER_INTERVAL，
  并在~24行对SIP（Supervisor Interrupt Pending，即S模式的中断等待寄存器）寄存器进行设置，将其中的SIP_SSIP位进行设置，完成后返回。
  至此，时钟中断在M态的处理就结束了，剩下的动作交给S态继续处理。而handle_timer()在第~23行的动作，
  会导致PKE操作系统内核在S模式收到一个来自M态的时钟中断请求（CAUSE_MTIMER_S_TRAP） */
  int cpuid = 0;
  // setup the timer fired at next time (TIMER_INTERVAL from now)
  *(uint64*)CLINT_MTIMECMP(cpuid) = *(uint64*)CLINT_MTIMECMP(cpuid) + TIMER_INTERVAL;

  // setup a soft interrupt in sip (S-mode Interrupt Pending) to be handled in S-mode
  write_csr(sip, SIP_SSIP);
}

//
// handle_mtrap calls a handling function according to the type of a machine mode interrupt (trap).
//
void handle_mtrap() {
  uint64 mcause = read_csr(mcause);

  // added @lab1_challenge2 : 处理非法指令异常，打印源代码行号信息
  if(mcause != CAUSE_MTIMER) {
      uint64 mepc = read_csr(mepc);

      // 找到对应的源代码行号
      addr_line *lines = current->line;
      int count = current->line_ind;
      addr_line *hit = NULL;
      for (int i = 0; i < count; ++i) {
        if (lines[i].addr > mepc) break;
        hit = &lines[i];
      }
      if (!hit) { /* 没命中 */ }

      code_file *cur_file = &(current->file)[hit->file];
      char* dir = (current->dir)[cur_file->dir];
      char* file = cur_file->file;
      sprint("Runtime error at %s/%s:%d\n", dir, file, hit->line);

      char fullpath[256] = {'\0'};
      strcpy(fullpath, dir);
      strcat(fullpath, "/");
      strcat(fullpath, file);
      spike_file_t *src = spike_file_open(fullpath, O_RDONLY, 0);

      char line[500];
      size_t offset = 0;
      int cur_line = 0;
      char ch;
      while(spike_file_read(src, &ch, 1) == 1) {
        if (ch == '\n') {
          line[offset] = '\0';
          offset = 0;
          cur_line++;
          if (cur_line == hit->line) {
            sprint("%s\n", line);
            break;
          } else {
            // do nothing
          }
        } else {
          line[offset++] = ch;
        }
      }
      spike_file_close(src);
  }

  switch (mcause) {
    case CAUSE_MTIMER:
      handle_timer();
      break;
    case CAUSE_FETCH_ACCESS:
      handle_instruction_access_fault();
      break;
    case CAUSE_LOAD_ACCESS:
      handle_load_access_fault();
    case CAUSE_STORE_ACCESS:
      handle_store_access_fault();
      break;
    case CAUSE_ILLEGAL_INSTRUCTION:
      // TODO (lab1_2): call handle_illegal_instruction to implement illegal instruction
      // interception, and finish lab1_2.
      // panic( "call handle_illegal_instruction to accomplish illegal instruction interception for lab1_2.\n" );
      handle_illegal_instruction();
      break;
    case CAUSE_MISALIGNED_LOAD:
      handle_misaligned_load();
      break;
    case CAUSE_MISALIGNED_STORE:
      handle_misaligned_store();
      break;

    default:
      sprint("machine trap(): unexpected mscause %p\n", mcause);
      sprint("            mepc=%p mtval=%p\n", read_csr(mepc), read_csr(mtval));
      panic( "unexpected exception happened in M-mode.\n" );
      break;
  }
}
