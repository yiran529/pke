/*
 * Utility functions for process management. 
 *
 * Note: in Lab1, only one process (i.e., our user application) exists. Therefore, 
 * PKE OS at this stage will set "current" to the loaded user application, and also
 * switch to the old "current" process after trap handling.
 */

#include "riscv.h"
#include "strap.h"
#include "config.h"
#include "process.h"
#include "elf.h"
#include "string.h"
#include "vmm.h"
#include "pmm.h"
#include "memlayout.h"
#include "sched.h"
#include "spike_interface/spike_utils.h"

//Two functions defined in kernel/usertrap.S
extern char smode_trap_vector[];
extern void return_to_user(trapframe *, uint64 satp);

// trap_sec_start points to the beginning of S-mode trap segment (i.e., the entry point
// of S-mode trap vector).
extern char trap_sec_start[];

// process pool. added @lab3_1
process procs[NPROC];

// current points to the currently running user-mode application.
process* current = NULL;

//
// switch to a user-mode process
//
void switch_to(process* proc) {
  assert(proc);
  current = proc;

  // write the smode_trap_vector (64-bit func. address) defined in kernel/strap_vector.S
  // to the stvec privilege register, such that trap handler pointed by smode_trap_vector
  // will be triggered when an interrupt occurs in S mode.
  write_csr(stvec, (uint64)smode_trap_vector);

  // set up trapframe values (in process structure) that smode_trap_vector will need when
  // the process next re-enters the kernel.
  proc->trapframe->kernel_sp = proc->kstack;      // process's kernel stack
  proc->trapframe->kernel_satp = read_csr(satp);  // kernel page table
  proc->trapframe->kernel_trap = (uint64)smode_trap_handler;

  // SSTATUS_SPP and SSTATUS_SPIE are defined in kernel/riscv.h
  // set S Previous Privilege mode (the SSTATUS_SPP bit in sstatus register) to User mode.
  unsigned long x = read_csr(sstatus);
  x &= ~SSTATUS_SPP;  // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE;  // enable interrupts in user mode

  // write x back to 'sstatus' register to enable interrupts, and sret destination mode.
  write_csr(sstatus, x);

  // set S Exception Program Counter (sepc register) to the elf entry pc.
  write_csr(sepc, proc->trapframe->epc);

  // make user page table. macro MAKE_SATP is defined in kernel/riscv.h. added @lab2_1
  uint64 user_satp = MAKE_SATP(proc->pagetable);

  // sprint("\n>>> Switching to USER mode: satp will change <<<\n");
  // sprint("Before: satp = 0x%lx (kernel page table)\n", read_csr(satp));
  // sprint("After:  satp = 0x%lx (user page table)\n", user_satp);
  // sprint("Result: All memory accesses auto-use user page table\n\n");

  // return_to_user() is defined in kernel/strap_vector.S. switch to user mode with sret.
  // note, return_to_user takes two parameters @ and after lab2_1.
  return_to_user(proc->trapframe, user_satp);
}

//
// initialize process pool (the procs[] array). added @lab3_1
//
void init_proc_pool() {
  memset( procs, 0, sizeof(process)*NPROC );

  for (int i = 0; i < NPROC; ++i) {
    procs[i].status = FREE;
    procs[i].pid = i;
  }
}

//
// allocate an empty process, init its vm space. returns the pointer to
// process strcuture. added @lab3_1
//
process* alloc_process() {
  // locate the first usable process structure
  int i;

  for( i=0; i<NPROC; i++ )
    if( procs[i].status == FREE ) break;

  if( i>=NPROC ){
    panic( "cannot find any free process structure.\n" );
    return 0;
  }

  // init proc[i]'s vm space
  procs[i].trapframe = (trapframe *)alloc_page();  //trapframe, used to save context
  memset(procs[i].trapframe, 0, sizeof(trapframe));

  // page directory
  procs[i].pagetable = (pagetable_t)alloc_page();
  memset((void *)procs[i].pagetable, 0, PGSIZE);

  procs[i].kstack = (uint64)alloc_page() + PGSIZE;   //user kernel stack top
  uint64 user_stack = (uint64)alloc_page();       //phisical address of user stack bottom
  procs[i].trapframe->regs.sp = USER_STACK_TOP;  //virtual address of user stack top

  // allocates a page to record memory regions (segments)
  procs[i].mapped_info = (mapped_region*)alloc_page();
  memset( procs[i].mapped_info, 0, PGSIZE );

  // map user stack in userspace
  user_vm_map((pagetable_t)procs[i].pagetable, USER_STACK_TOP - PGSIZE, PGSIZE,
    user_stack, prot_to_type(PROT_WRITE | PROT_READ, 1));
  procs[i].mapped_info[STACK_SEGMENT].va = USER_STACK_TOP - PGSIZE;
  procs[i].mapped_info[STACK_SEGMENT].npages = 1;
  procs[i].mapped_info[STACK_SEGMENT].seg_type = STACK_SEGMENT;

  // map trapframe in user space (direct mapping as in kernel space).
  user_vm_map((pagetable_t)procs[i].pagetable, (uint64)procs[i].trapframe, PGSIZE,
    (uint64)procs[i].trapframe, prot_to_type(PROT_WRITE | PROT_READ, 0));
  procs[i].mapped_info[CONTEXT_SEGMENT].va = (uint64)procs[i].trapframe;
  procs[i].mapped_info[CONTEXT_SEGMENT].npages = 1;
  procs[i].mapped_info[CONTEXT_SEGMENT].seg_type = CONTEXT_SEGMENT;

  // map S-mode trap vector section in user space (direct mapping as in kernel space)
  // we assume that the size of usertrap.S is smaller than a page.
  user_vm_map((pagetable_t)procs[i].pagetable, (uint64)trap_sec_start, PGSIZE,
    (uint64)trap_sec_start, prot_to_type(PROT_READ | PROT_EXEC, 0));
  procs[i].mapped_info[SYSTEM_SEGMENT].va = (uint64)trap_sec_start;
  procs[i].mapped_info[SYSTEM_SEGMENT].npages = 1;
  procs[i].mapped_info[SYSTEM_SEGMENT].seg_type = SYSTEM_SEGMENT;

  sprint("in alloc_proc. user frame 0x%lx, user stack 0x%lx, user kstack 0x%lx \n",
    procs[i].trapframe, procs[i].trapframe->regs.sp, procs[i].kstack);

  // initialize the process's heap manager
  procs[i].user_heap.heap_top = USER_FREE_ADDRESS_START;
  procs[i].user_heap.heap_bottom = USER_FREE_ADDRESS_START;
  procs[i].user_heap.free_pages_count = 0;

  // map user heap in userspace
  procs[i].mapped_info[HEAP_SEGMENT].va = USER_FREE_ADDRESS_START;
  procs[i].mapped_info[HEAP_SEGMENT].npages = 0;  // no pages are mapped to heap yet.
  procs[i].mapped_info[HEAP_SEGMENT].seg_type = HEAP_SEGMENT;

  procs[i].total_mapped_region = 4;

  // initialize files_struct
  procs[i].pfiles = init_proc_file_management();
  sprint("in alloc_proc. build proc_file_management successfully.\n");

  // return after initialization.
  return &procs[i];
}

//
// refresh a process, reclaim its resources. added @lab3_1
//
void refresh_process(process* proc) {
  memset(proc->trapframe, 0, sizeof(trapframe));

  // page directory
  proc->pagetable = (pagetable_t)alloc_page();
  memset((void *)proc->pagetable, 0, PGSIZE);
  uint64 user_stack = (uint64)alloc_page();       //phisical address of user stack bottom
  proc->trapframe->regs.sp = USER_STACK_TOP;  //virtual address of user stack top

  // allocates a page to record memory regions (segments)
  memset( proc->mapped_info, 0, PGSIZE );
  // map user stack in userspace
  user_vm_map((pagetable_t)proc->pagetable, USER_STACK_TOP - PGSIZE, PGSIZE,
    user_stack, prot_to_type(PROT_WRITE | PROT_READ, 1));
  proc->mapped_info[STACK_SEGMENT].va = USER_STACK_TOP - PGSIZE;
  proc->mapped_info[STACK_SEGMENT].npages = 1;
  proc->mapped_info[STACK_SEGMENT].seg_type = STACK_SEGMENT;

  // map trapframe in user space (direct mapping as in kernel space).
  user_vm_map((pagetable_t)proc->pagetable, (uint64)proc->trapframe, PGSIZE,
    (uint64)proc->trapframe, prot_to_type(PROT_WRITE | PROT_READ, 0));
  proc->mapped_info[CONTEXT_SEGMENT].va = (uint64)proc->trapframe;
  proc->mapped_info[CONTEXT_SEGMENT].npages = 1;
  proc->mapped_info[CONTEXT_SEGMENT].seg_type = CONTEXT_SEGMENT;

  // map S-mode trap vector section in user space (direct mapping as in kernel space)
  // we assume that the size of usertrap.S is smaller than a page.
  user_vm_map((pagetable_t)proc->pagetable, (uint64)trap_sec_start, PGSIZE,
    (uint64)trap_sec_start, prot_to_type(PROT_READ | PROT_EXEC, 0));
  proc->mapped_info[SYSTEM_SEGMENT].va = (uint64)trap_sec_start;
  proc->mapped_info[SYSTEM_SEGMENT].npages = 1;
  proc->mapped_info[SYSTEM_SEGMENT].seg_type = SYSTEM_SEGMENT;
  sprint("refresh process %d: user frame 0x%lx, user stack 0x%lx, user kstack 0x%lx \n",
    proc->pid, proc->trapframe, proc->trapframe->regs.sp, proc->kstack);

  // initialize the process's heap manager
  proc->user_heap.heap_top = USER_FREE_ADDRESS_START;
  proc->user_heap.heap_bottom = USER_FREE_ADDRESS_START;
  proc->user_heap.free_pages_count = 0;

  // map user heap in userspace
  proc->mapped_info[HEAP_SEGMENT].va = USER_FREE_ADDRESS_START;
  proc->mapped_info[HEAP_SEGMENT].npages = 0;  // no pages are mapped to heap yet.
  proc->mapped_info[HEAP_SEGMENT].seg_type = HEAP_SEGMENT;

  proc->total_mapped_region = 4;
  // initialize files_struct
  // proc->pfiles = init_proc_file_management();
  // we might not need to re-initialize proc_file_management here
  // sprint("Successfully refresh process %d.\n", proc->pid);

}

//
// reclaim a process. added @lab3_1
//
int free_process( process* proc ) {
  // we set the status to ZOMBIE, but cannot destruct its vm space immediately.
  // since proc can be current process, and its user kernel stack is currently in use!
  // but for proxy kernel, it (memory leaking) may NOT be a really serious issue,
  // as it is different from regular OS, which needs to run 7x24.
  proc->status = ZOMBIE;

  return 0;
}

//
// implements fork syscal in kernel. added @lab3_1
// basic idea here is to first allocate an empty process (child), then duplicate the
// context and data segments of parent process to the child, and lastly, map other
// segments (code, system) of the parent to child. the stack segment remains unchanged
// for the child.
//
int do_fork( process* parent)
{
  sprint( "will fork a child from parent %d.\n", parent->pid );
  process* child = alloc_process();

  for( int i=0; i<parent->total_mapped_region; i++ ){
    // browse parent's vm space, and copy its trapframe and data segments,
    // map its code segment.
    switch( parent->mapped_info[i].seg_type ){
      case CONTEXT_SEGMENT:
        *child->trapframe = *parent->trapframe;
        break;
      case STACK_SEGMENT:
        memcpy( (void*)lookup_pa(child->pagetable, child->mapped_info[STACK_SEGMENT].va),
          (void*)lookup_pa(parent->pagetable, parent->mapped_info[i].va), PGSIZE );
        break;
      case HEAP_SEGMENT: {
        // build a same heap for child process.

        // convert free_pages_address into a filter to skip reclaimed blocks in the heap
        // when mapping the heap blocks
        int free_block_filter[MAX_HEAP_PAGES]; /// 标记哪些堆页是被释放的数组
        memset(free_block_filter, 0, MAX_HEAP_PAGES);
        uint64 heap_bottom = parent->user_heap.heap_bottom;

        /// 标记已释放的堆页
        for (int i = 0; i < parent->user_heap.free_pages_count; i++) {
          int index = (parent->user_heap.free_pages_address[i] - heap_bottom) / PGSIZE;
          free_block_filter[index] = 1;
        }

        // copy and map the heap blocks /// 跳过已释放的堆页
        for (uint64 heap_block = current->user_heap.heap_bottom;
             heap_block < current->user_heap.heap_top; heap_block += PGSIZE) {
          if (free_block_filter[(heap_block - heap_bottom) / PGSIZE])  // skip free blocks
            continue;

          void* child_pa = alloc_page();
          memcpy(child_pa, (void*)lookup_pa(parent->pagetable, heap_block), PGSIZE);

          /// 以下语句的执行结果：
          /// 虚拟地址：父、子一致（指针数值不变）
          /// 物理页面：父、子各自独立（复制，不共享）
          /// 已释放页：跳过不映射，保持“空洞”一致
          user_vm_map((pagetable_t)child->pagetable, heap_block, PGSIZE, (uint64)child_pa,
                      prot_to_type(PROT_WRITE | PROT_READ, 1));
        }

        child->mapped_info[HEAP_SEGMENT].npages = parent->mapped_info[HEAP_SEGMENT].npages;

        // copy the heap manager from parent to child
        memcpy((void*)&child->user_heap, (void*)&parent->user_heap, sizeof(parent->user_heap));
        break;
      }
      case CODE_SEGMENT: {
        // TODO (lab3_1): implment the mapping of child code segment to parent's
        // code segment.
        // hint: the virtual address mapping of code segment is tracked in mapped_info
        // page of parent's process structure. use the information in mapped_info to
        // retrieve the virtual to physical mapping of code segment.
        // after having the mapping information, just map the corresponding virtual
        // address region of child to the physical pages that actually store the code
        // segment of parent process.
        // DO NOT COPY THE PHYSICAL PAGES, JUST MAP THEM.
        // panic( "You need to implement the code segment mapping of child in lab3_1.\n" );
        
        // uint64 parent_va = current->mapped_info[CODE_SEGMENT].va;
        // uint64 child_va = parent_va;
        // void* child_pa = (void*)lookup_pa(current->pagetable, child_va);
        // user_vm_map((pagetable_t)child->pagetable, child_va, PGSIZE, (uint64)child_pa,
        //               prot_to_type(PROT_EXEC | PROT_READ, 1));

        // 注意不同于STACK_SEGMENT或CONTEXT_SEGMENT的是，CODE_SEGMENT可能不止1 npages，所以需要便利
        uint64 va_start = parent->mapped_info[i].va;
        int npages = parent->mapped_info[i].npages;
        for (int p = 0; p < npages; p++) {
          uint64 va = va_start + p * PGSIZE;
          void* pa = (void*)lookup_pa(parent->pagetable, va);
          user_vm_map(child->pagetable, va, PGSIZE, (uint64)pa,
                      prot_to_type(PROT_EXEC | PROT_READ, 1));
        }

        // after mapping, register the vm region (do not delete codes below!)
        child->mapped_info[child->total_mapped_region].va = parent->mapped_info[i].va;
        child->mapped_info[child->total_mapped_region].npages =
          parent->mapped_info[i].npages;
        child->mapped_info[child->total_mapped_region].seg_type = CODE_SEGMENT;
        child->total_mapped_region++;
        break;
      }
      case DATA_SEGMENT: {
        uint64 va_start = parent->mapped_info[i].va;
        int npages = parent->mapped_info[i].npages;
        for (int p = 0; p < npages; p++) {
          uint64 va = va_start + p * PGSIZE;
          void* child_pa = alloc_page();
          memcpy(child_pa, (void*)lookup_pa(parent->pagetable, va), PGSIZE);
          user_vm_map(child->pagetable, va, PGSIZE, (uint64)child_pa,
                      prot_to_type(PROT_WRITE | PROT_READ, 1));
        }

        child->mapped_info[child->total_mapped_region].va = parent->mapped_info[i].va;
        child->mapped_info[child->total_mapped_region].npages =
          parent->mapped_info[i].npages;
        child->mapped_info[child->total_mapped_region].seg_type = DATA_SEGMENT;
        child->total_mapped_region++;
        break;
      }
    }
  }

  sprint("do_fork map code segment at pa:%lx of parent %d to child %d at va:0x%lx.\n",
    lookup_pa(parent->pagetable, parent->mapped_info[CODE_SEGMENT].va),
    parent->pid, child->pid, parent->mapped_info[CODE_SEGMENT].va );

  child->status = READY;
  child->trapframe->regs.a0 = 0;
  child->parent = parent;
  insert_to_ready_queue( child );

  return child->pid;
}

int do_exec( process* proc, char* pathname, char* argv ) {
  sprint( "will exec a new program %s in process %d.\n", pathname, proc->pid );

  /*
   * -----------------------------------------------------------------
   * 保存旧进程的堆信息，以便后续释放旧堆页面时使用
   * -----------------------------------------------------------------
   */
  
  pagetable_t old_pagetable = proc->pagetable;
  process_heap_manager old_heap = proc->user_heap;
  mapped_region old_mapped_info[MAX_MAPPED_REGION]; 
  int old_total_mapped_region = proc->total_mapped_region;
  memcpy( old_mapped_info, proc->mapped_info, sizeof(mapped_region)*old_total_mapped_region );

  /*
   * -----------------------------------------------------------------
   * 刷新进程结构，加载新的 ELF 程序
   * -----------------------------------------------------------------
   */
  refresh_process( proc );
  load_bincode_from_host_elf_for_exec(proc, pathname);

  /*
   * -----------------------------------------------------------------
   * 设置新的命令行参数到用户栈
   * -----------------------------------------------------------------
   */
  // 设置命令行参数
  // 栈布局（从高地址到低地址）：
  // | argv[0] 字符串内容 (如果有) |
  // | NULL (argv数组结束标记)    |
  // | argv[0] 指针 (如果有)       | <- argv 指向这里
  
  uint64 sp = USER_STACK_TOP;
  int argc;
  uint64 argv_base = 0;
  
  // 计算参数个数（只传递一个字符串参数，不传递程序名）
  if (argv != NULL && argv[0] != '\0') {
    argc = 1;  // 只有1个参数
  } else {
    argc = 0;  // 没有参数
  }
  
  // 在栈上放置字符串（从高地址向低地址）
  if (argc == 1) {
    // 放置 argv[0] (唯一的参数)
    int arg_len = strlen(argv) + 1;  // 包含 '\0'
    sp -= arg_len;
    sp &= ~0x7;  // 8字节对齐
    uint64 arg_addr = sp;
    // 将参数字符串复制到用户栈
    char* dest = (char*)user_va_to_pa(proc->pagetable, (void*)sp);
    strcpy(dest, argv);
    
    // 放置 argv 指针数组
    sp -= sizeof(uint64);  // NULL 终止符
    uint64* null_ptr = (uint64*)user_va_to_pa(proc->pagetable, (void*)sp);
    *null_ptr = 0;
    
    // 放置参数指针
    sp -= sizeof(uint64);
    uint64* argv_ptr = (uint64*)user_va_to_pa(proc->pagetable, (void*)sp);
    *argv_ptr = arg_addr;
    
    argv_base = sp;  // argv 数组的起始地址
  }
  
  // 对齐栈指针到 16 字节（RISC-V ABI 要求）
  sp &= ~0xf;
  
  // 设置寄存器
  proc->trapframe->regs.a0 = argc;       // 第一个参数：argc
  proc->trapframe->regs.a1 = argv_base;  // 第二个参数：argv
  proc->trapframe->regs.sp = sp;         // 更新栈指针

  /* 原代码（包含程序名的版本）：
  // 计算参数个数
  if (argv != NULL && argv[0] != '\0') {
    argc = 2;  // 程序名 + 1个参数
  } else {
    argc = 1;  // 只有程序名
  }
  
  // 保存字符串在栈上的地址
  uint64 arg_addrs[2];
  
  // 在栈上放置字符串（从高地址向低地址）
  if (argc == 2) {
    // 放置 argv[1] (实际参数)
    int arg_len = strlen(argv) + 1;  // 包含 '\0'
    sp -= arg_len;
    sp &= ~0x7;  // 8字节对齐
    arg_addrs[1] = sp;
    // 将参数字符串复制到用户栈
    char* dest = (char*)user_va_to_pa(proc->pagetable, (void*)sp);
    strcpy(dest, argv);
  }
  
  // 放置 argv[0] (程序名)
  int pathname_len = strlen(pathname) + 1;
  sp -= pathname_len;
  sp &= ~0x7;  // 8字节对齐
  arg_addrs[0] = sp;
  char* dest0 = (char*)user_va_to_pa(proc->pagetable, (void*)sp);
  strcpy(dest0, pathname);
  
  // 放置 argv 指针数组
  sp -= sizeof(uint64);  // NULL 终止符
  uint64* null_ptr = (uint64*)user_va_to_pa(proc->pagetable, (void*)sp);
  *null_ptr = 0;
  
  // 放置参数指针
  for (int i = argc - 1; i >= 0; i--) {
    sp -= sizeof(uint64);
    uint64* argv_ptr = (uint64*)user_va_to_pa(proc->pagetable, (void*)sp);
    *argv_ptr = arg_addrs[i];
  }
  
  argv_base = sp;  // argv 数组的起始地址
  */

  /*
   * -----------------------------------------------------------------
   * 释放旧进程的堆页面和页表
   * -----------------------------------------------------------------
   */
  int free_block_filter[MAX_HEAP_PAGES]; /// 标记哪些堆页是被释放的数组
  memset(free_block_filter, 0, MAX_HEAP_PAGES);
  uint64 heap_bottom = old_heap.heap_bottom;
  /// 标记已释放的堆页
  for (int i = 0; i < old_heap.free_pages_count; i++) {
    int index = (old_heap.free_pages_address[i] - heap_bottom) / PGSIZE;
    free_block_filter[index] = 1;
  }
  // free old pagetable and heap pages
  for (uint64 heap_block = old_heap.heap_bottom;
             heap_block < old_heap.heap_top; heap_block += PGSIZE) {
    if (free_block_filter[(heap_block - heap_bottom) / PGSIZE])  // skip free blocks
      continue;

    free_page( (void*)lookup_pa(old_pagetable, heap_block) );
  }
  // free old user stack
  for (int i = 0; i < old_mapped_info[STACK_SEGMENT].npages; i++) {
    free_page( user_va_to_pa(old_pagetable, (void*)(uint64)(USER_STACK_TOP - PGSIZE - i * PGSIZE)) );
  }
  // free old pagetable
  free_page( (void*)old_pagetable );
  // sprint("Exec completed for process %d with argc=%d.\n", proc->pid, argc);
  
  return 0;
}

int do_wait(int pid) {
  // sprint("[DEBUG] Entered do_wait\n");
  // pid不合法
  if(pid < -1 || pid == 0) return -1;

  // 等待特定pid的进程
  if(pid > 0) {
    for(int i = 0; i < NPROC; i++) {
      process* p = &procs[i];

      if(!p->parent) continue;
      if(p->parent->pid != current->pid && p->pid == pid) {
        return -1;
      }
      if(p->parent->pid == current->pid && p->pid == pid) {
        if (p->status != ZOMBIE) {
          current->status = BLOCKED;
          schedule();
        }
        assert(p->status == ZOMBIE);
        // user_vm_unmap(p.pagetable, ) ???
        // current->status = BLOCKED; ???
        // p->status = FREE;
        return p->pid;
      }
    }
  }

  // 等待任意一个子进程
  if(pid == -1) {
    // sprint("[DEBUG] loop to find a ZOMBIE children process\n");
    int has_children = 0;

    while(1) {
      for(int i = 0; i < NPROC; i++) {
        // sprint("[DEBUG] Check if procs[%d] satisfies\n", i);
        process *p = &procs[i];
        if(!p->parent) continue;
        if(p->parent->pid == current->pid) {
          has_children = 1;
        }
        if(p->parent->pid == current->pid && p->status == ZOMBIE) {
          // user_vm_unmap(p.pagetable, ) ???
          // current->status = BLOCKED; ???
          // p->status = FREE;
          return p->pid;
        }
      }
      current->status=BLOCKED;
      schedule();
    }

    if(!has_children) {
      return -1;
    }
  }

  return -1;
}