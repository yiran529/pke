#ifndef _PROC_H_
#define _PROC_H_

#include "riscv.h"
#include "proc_file.h"

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

// riscv-pke kernel supports at most 32 processes
#define NPROC 32
// maximum number of pages in a process's heap
#define MAX_HEAP_PAGES 32

// possible status of a process
enum proc_status {
  FREE,            // unused state
  READY,           // ready state
  RUNNING,         // currently running
  BLOCKED,         // waiting for something
  ZOMBIE,          // terminated but not reclaimed yet
};

// types of a segment
enum segment_type {
  STACK_SEGMENT = 0,   // runtime stack segment
  CONTEXT_SEGMENT, // trapframe segment
  SYSTEM_SEGMENT,  // system segment
  HEAP_SEGMENT,    // runtime heap segment
  CODE_SEGMENT,    // ELF segment
  DATA_SEGMENT,    // ELF segment
};

#define MAX_MAPPED_REGION DATA_SEGMENT + 1

// the VM regions mapped to a user process
typedef struct mapped_region {
  uint64 va;       // mapped virtual address
  uint32 npages;   // mapping_info is unused if npages == 0
  uint32 seg_type; // segment type, one of the segment_types
} mapped_region;

typedef struct process_heap_manager {
  // points to the last free page in our simple heap.
  uint64 heap_top;
  // points to the bottom of our simple heap.
  uint64 heap_bottom;

  // the address of free pages in the heap
  uint64 free_pages_address[MAX_HEAP_PAGES];
  // the number of free pages in the heap
  uint32 free_pages_count;
}process_heap_manager;

// the extremely simple definition of process, used for begining labs of PKE
typedef struct process_t {
  // pointing to the stack used in trap handling.
  uint64 kstack;
  // user page table
  pagetable_t pagetable;
  // trapframe storing the context of a (User mode) process.
  trapframe* trapframe;

  // points to a page that contains mapped_regions. below are added @lab3_1
  mapped_region *mapped_info;
  // next free mapped region in mapped_info
  int total_mapped_region;

  // heap management
  process_heap_manager user_heap;

  // process id
  uint64 pid;
  // process status
  int status;
  // parent process
  struct process_t *parent;
  // next queue element
  struct process_t *queue_next;

  // accounting. added @lab3_3
  int tick_count;

  // file system. added @lab4_1
  proc_file_management *pfiles;
}process;

// switch to run user app
void switch_to(process*);

// initialize process pool (the procs[] array)
void init_proc_pool();
// allocate an empty process, init its vm space. returns its pid
process* alloc_process();
// reclaim a process, destruct its vm space and free physical pages.
int free_process( process* proc );
// fork a child from parent
int do_fork(process* parent);
void refresh_process(process* proc);
int do_exec(process* proc, char *pathname);

// current running process
extern process* current;

#endif
