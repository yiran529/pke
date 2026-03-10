#ifndef _PROC_H_
#define _PROC_H_

#include "riscv.h"
#include "proc_file.h"
#include "config.h"

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

// added @lab1_challenge2
// code file struct, including directory index and file name char pointer
typedef struct {
    uint64 dir; char *file;
} code_file;
// address-line number-file name table
typedef struct {
    uint64 addr, line, file;
} addr_line;

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

// added @lab2_challenge1
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

////////////////////////////

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

  // added @lab2_2: for better_malloc
  // heap base pa 
  uint64 heap_pa;
  // added @lab2_challenge2: for better_malloc and better_free
  // heap base va
  uint64 heap_va; 
  // current heap size in bytes
  uint64 heap_size;

  // added @lab2_challenge1
  uint64 user_st_top;

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

  // executable path, for backtrace etc.
  char exe_path[128];

  // added @lab1_challenge2
  char *debugline; char **dir; code_file *file; addr_line *line; int line_ind;

  // kmalloc heap mark: saved before loading debug info, reset on process free
  uint64 debug_heap_mark;
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
int do_exec(process* proc, char *pathname, char *argv);

int do_wait(int pid);

// Each hart keeps its own current pointer to avoid clobbering another hart's context.
// In multicore, timer/syscall traps must return to the process that was running on that
// hart, so we store per-hart 'current'.
// address of the first free page in our simple heap. added @lab2_2
extern process* current[NCPU];

#endif
