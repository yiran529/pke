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
#include "sched.h"
#include "proc_file.h"
#include "elf.h"
#include "kernel.h"
#include "config.h"

#include "spike_interface/spike_utils.h"

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
  // sprint("Current[hid] satp:  0x%lx (points to KERNEL page table)\n", read_csr(satp));
  // sprint("User pagetable: 0x%lx\n", (uint64)current[hid]->pagetable);
  // sprint("Why manual? satp != user_pagetable, so MMU can't auto-translate buf!\n");
  // sprint("Calling user_va_to_pa() to manually walk user page table...\n");
  
  char* pa = (char*)user_va_to_pa((pagetable_t)(p->pagetable), (void*)buf);
  // sprint(pa);
  // Add hartid to prints so we can distinguish which hart produced the user message.
  sprint("%s", pa);
  // sprint("Translated PA: 0x%lx\n", (uint64)pa);
  // sprint("Message: %s", pa);
  // sprint("===========================================\n\n");
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  int hid = read_tp();
  sprint("hartid = %d: User exit with code: %d.\n", hid, code);

  process* parent = current[hid]->parent;
  if (parent != NULL && parent->status == BLOCKED) {
    // Only wake the parent if it is blocked in wait():
    // wait(-1) => WAITPID_ANY_CHILD, wait(pid) => waiting_pid == pid.
    if (parent->waiting_pid == WAITPID_ANY_CHILD ||
        parent->waiting_pid == (int)current[hid]->pid) {
      insert_to_ready_queue(parent);   // 唤醒等待的父进程
    }
  }
    
  // Mark the current process as ZOMBIE and schedule the next one.
  // When no runnable processes remain, schedule() will call shutdown().
  current[hid]->status = ZOMBIE;
  schedule();

  // should never reach here
  return 0;
}

// added @lab1_challenge1
ssize_t sys_user_print_backtrace(uint32 nlayers) {
  int hid = read_tp();
  // sprint("[DEBUG] hartid = %d: sys_user_print_backtrace called with nlayers=%d\n", hid, nlayers);
  // 重新打开 ELF 文件
  arg_buf arg_bug_msg;
  size_t argc = parse_args(&arg_bug_msg);  // 需要将 parse_args 改为非 static

  elf_ctx elfloader;
  elf_info info;
  
  info.f = vfs_open(current[hid]->exe_path, O_RDONLY);
  // sprint("[DEBUG] hartid = %d: Opened ELF file for backtrace: %s\n", hid, current[hid]->exe_path);
  info.p = current[hid];
  
  if (IS_ERR_VALUE(info.f)) {
    sprint("hartid = %d: Failed to open ELF file\n", hid);
    return -1;
  }
  
  if (elf_init_vfs(&elfloader, &info) != EL_OK) {
    vfs_close(info.f);
    return -1;
  }

  uint64 fp = current[hid]->trapframe->regs.s0; // fp寄存器
  uint64 ra = current[hid]->trapframe->regs.ra; 
  elf_section_header section_headers[20]; //理论上限远不止20，这里为了方便而进行简化

  // fp 是用户态虚拟地址，内核中必须通过 user_va_to_pa 转换后才能解引用
  //
  // ecall 时的寄存器状态:
  //   ra = 返回到 print_backtrace 中的地址
  //   s0 = do_user_call 的帧指针
  //
  // do_user_call 的帧布局 (只保存了 s0，没保存 ra):
  //   *(s0 - 8)  = saved old s0 = print_backtrace 的帧指针
  //
  // 正常函数的帧布局:
  // 高地址
  // -----------------
  // 调用者栈帧
  // ----------------- ← fp (= 旧 sp)
  // 返回地址 (ra)
  // 旧的 fp
  // 局部变量
  // 临时空间
  // ----------------- ← sp
  // 低地址
  //
  if (fp == 0) goto done;

  // 步骤1: 从 do_user_call 的帧中取出 print_backtrace 的 fp
  // do_user_call 只在 *(s0-8) 保存了 old s0，没有保存 ra
  fp = *(uint64*)user_va_to_pa((pagetable_t)(current[hid]->pagetable), (void*)(fp - 8));
  // 此时 ra 仍指向 print_backtrace 内部 (trapframe->ra)，fp 是 print_backtrace 的帧指针

  // 步骤2: 跳过 print_backtrace，直接到它的调用者 (f8)
  if (fp == 0) goto done;
  ra = *(uint64*)user_va_to_pa((pagetable_t)(current[hid]->pagetable), (void*)(fp - 8));
  fp = *(uint64*)user_va_to_pa((pagetable_t)(current[hid]->pagetable), (void*)(fp - 16));

  // 步骤3: 正常遍历剩余帧
  while(fp != 0 && get_name_by_ra(&elfloader, section_headers, ra)) {
     uint64 new_ra = *(uint64*)user_va_to_pa((pagetable_t)(current[hid]->pagetable), (void*)(fp - 8));
     uint64 new_fp = *(uint64*)user_va_to_pa((pagetable_t)(current[hid]->pagetable), (void*)(fp - 16));
     ra = new_ra;
     fp = new_fp;
     nlayers--;
     if (nlayers == 0) break;
  }
done:
  vfs_close(info.f);
  return 0;
}

////////////////////////////////////////
/* Below are functions for lab2_challenge2 */
// 扩展堆空间，返回扩展后的新空间起始VA，失败返回0
uint64 expand_heap(process *p, uint64 needed_size) {
  // 计算需要扩展的页数
  uint64 expand_bytes = ALIGN_UP(needed_size, PGSIZE);
  uint64 new_va = p->heap_va + p->heap_size;  // 新页的VA
  
  // 逐页分配并映射
  for (uint64 i = 0; i < expand_bytes; i += PGSIZE) {
    void *new_pa = alloc_page();
    if (!new_pa) {
      return 0;  // 物理内存不足
    }
    memset(new_pa, 0, PGSIZE);
    
    // 映射到用户地址空间
    user_vm_map((pagetable_t)p->pagetable, new_va + i, PGSIZE, (uint64)new_pa,
                prot_to_type(PROT_WRITE | PROT_READ, 1));
    // sprint("[DEBUG] Mapped new page: VA 0x%lx to PA 0x%lx\n", new_va + i, (uint64)new_pa);
  }
  
  // 将新空间初始化为一个大的空闲块
  uint64 old_heap_end = p->heap_va + p->heap_size;
  heap_chunk_t *new_chunk = (heap_chunk_t *)user_va_to_pa(p->pagetable, (void*)old_heap_end);
  new_chunk->size = expand_bytes;
  new_chunk->prev_size = 0;  // 需要根据前一块设置
  new_chunk->flags = 0;  // free
  
  // 尝试与前一个块合并（如果前一个块是空闲的）
  uint64 last_chunk_va = p->heap_va;
  uint64 heap_end = old_heap_end;
  
  // 找到最后一个块
  while (last_chunk_va < heap_end) {
    heap_chunk_t *hdr = (heap_chunk_t *)user_va_to_pa(p->pagetable, (void*)last_chunk_va);
    if (!hdr || hdr->size < CHUNK_MIN_SIZE) break;
    if (last_chunk_va + hdr->size < heap_end) {
      last_chunk_va += hdr->size;
      continue;
    }

    // 这是最后一个块
    if (CHUNK_IS_FREE(hdr)) {
      // 合并：扩展最后一个空闲块
      hdr->size += expand_bytes;
      new_chunk = hdr;
      old_heap_end = last_chunk_va;
    } else {
      // 最后一个块不是空闲的，设置新块的prev_size
      new_chunk->prev_size = hdr->size;
    }
    break;
  }
  
  p->heap_size += expand_bytes;
  return old_heap_end;
}

uint64 find_first_fit(process *p, uint64 size) {
  uint64 va = p->heap_va;
  int hid = read_tp();
  // sprint("[DEBUG] hartid = %d: Finding first fit for size %d in simple heap starting at va 0x%lx\n", hid, size, p->heap_va);
  uint64 heap_limit = p->heap_va + p->heap_size; // 简单堆当前仅一页
  while (va + CHUNK_HDR_SIZE <= heap_limit) {
    // VA -> PA 再访问
    heap_chunk_t *hdr = (heap_chunk_t *)user_va_to_pa(p->pagetable, (void*)va);
    // sprint("[DEBUG] hartid = %d: p: 0x%lx, pagetable: 0x%lx\n", hid, (uint64)p, (uint64)p->pagetable);
    // sprint("[DEBUG] hartid = %d: Checking chunk at va 0x%lx(pa: 0x%lx): size %d, flags %d\n", hid, va, (uint64)hdr, hdr ? hdr->size : 0, hdr ? hdr->flags : 0);
    if (!hdr) break; // 未映射，异常
    if (CHUNK_IS_FREE(hdr) && hdr->size - CHUNK_HDR_SIZE >= ALIGN_UP(size, CHUNK_ALIGN) ) { // free 且足够大
      hdr->flags = 1; // 标记为 used
      if(hdr->size - ALIGN_UP(size + CHUNK_HDR_SIZE, CHUNK_ALIGN) < CHUNK_MIN_SIZE) {
        size = hdr->size - CHUNK_HDR_SIZE; // 不足以拆分，全部分配
        hdr->size = ALIGN_UP(size + CHUNK_HDR_SIZE, CHUNK_ALIGN); // 调整大小
      } else {
        // 拆分
        uint64 next_va = va + ALIGN_UP(size + CHUNK_HDR_SIZE, CHUNK_ALIGN);
        heap_chunk_t *next_hdr = (heap_chunk_t *)user_va_to_pa(p->pagetable, (void*)next_va);
        next_hdr->size = hdr->size - ALIGN_UP(size + CHUNK_HDR_SIZE, CHUNK_ALIGN);
        next_hdr->prev_size = ALIGN_UP(size + CHUNK_HDR_SIZE, CHUNK_ALIGN);
        next_hdr->flags = 0; // free
        hdr->size = ALIGN_UP(size + CHUNK_HDR_SIZE, CHUNK_ALIGN);
      }
      return va; // 返回块头 VA，或返回数据区 va+CHUNK_HDR_SIZE
    }
    if (hdr->size < CHUNK_MIN_SIZE) break; // 防御：坏块
    va += hdr->size;
  }
  return 0; // 未找到
}

uint64 sys_user_allocate_for_better_malloc(int n) {
  int hid = read_tp();
  // void* pa = alloc_page();
  // /*取当前“用户简单堆”指针的当前位置作为本次分配的虚拟页起始地址。
  // g_ufree_page 是一个单调递增游标，表示下一个可用的用户虚拟地址（位于用户进程的“自由区”起点之后）*/
  // uint64 va = g_ufree_page;
  // g_ufree_page += PGSIZE;
  // user_vm_map((pagetable_t)current[hid]->pagetable, va, PGSIZE, (uint64)pa,
  //        prot_to_type(PROT_WRITE | PROT_READ, 1));
  uint64 va = find_first_fit(current[hid], n);
  if (va == 0) {
    // 没有合适的空闲块，尝试扩展堆
    uint64 needed = ALIGN_UP(n + CHUNK_HDR_SIZE, CHUNK_ALIGN);
    if (expand_heap(current[hid], needed) == 0) {
      // 扩展失败
      panic("Failed to expand heap or out of memory!\n");
    }
    
    // 重新尝试分配
    va = find_first_fit(current[hid], n);
    if (va == 0) {
      panic("Failed to allocate after heap expansion!\n");
    }
  }

  return va + CHUNK_HDR_SIZE; // 返回数据区地址(payload)
}

void collesce_forward(process *p, heap_chunk_t *hdr, uint64 va) {
  uint64 next_va = va + hdr->size;
  if (next_va + CHUNK_HDR_SIZE > p->heap_va + p->heap_size) return; // 越界

  heap_chunk_t *next_hdr = (heap_chunk_t *)user_va_to_pa(p->pagetable, (void*)next_va);
  if (!next_hdr) return; // 未映射

  if (CHUNK_IS_FREE(next_hdr)) {
    // 合并
    hdr->size += next_hdr->size;
    uint64 next_next_va = next_va + next_hdr->size;

    // 处理下下个块的prev_size更新
    if( next_next_va + CHUNK_HDR_SIZE > p->heap_va + p->heap_size) return; // 越界
    heap_chunk_t *next_next_hdr = (heap_chunk_t *)user_va_to_pa(p->pagetable, (void*)next_next_va);
    if (!next_next_hdr) return; // 未映射
    next_next_hdr->prev_size = hdr->size;
  }
}

void collesce_backward(process *p, heap_chunk_t *hdr, uint64 va) {
  if (va == p->heap_va) return; // 已经是第一个块，无法向前合并

  uint64 prev_va = va - hdr->prev_size;
  heap_chunk_t *prev_hdr = (heap_chunk_t *)user_va_to_pa(p->pagetable, (void*)prev_va);
  if (!prev_hdr) return; // 未映射

  if (CHUNK_IS_FREE(prev_hdr)) {
    // 合并
    prev_hdr->size += hdr->size;
    uint64 next_va = va + hdr->size;

    // 处理下个块的prev_size更新
    if( next_va + CHUNK_HDR_SIZE > p->heap_va + p->heap_size) return; // 越界
    heap_chunk_t *next_hdr = (heap_chunk_t *)user_va_to_pa(p->pagetable, (void*)next_va);
    if (!next_hdr) return; // 未映射
    next_hdr->prev_size = prev_hdr->size;
  }
}

//
// reclaim a page, indicated by "va". added @lab2_2
//
uint64 sys_user_free_for_better_free(uint64 va) {
  int hid = read_tp();
  // TODO : 如果va是非法的???????
  uint64 actual_va = va - CHUNK_HDR_SIZE;
  heap_chunk_t *hdr = (heap_chunk_t *)user_va_to_pa(current[hid]->pagetable, (void*)actual_va);
  hdr->flags = 0; // 标记为 free
  // 向前向后合并空闲块
  collesce_forward(current[hid], hdr, actual_va);
  collesce_backward(current[hid], hdr, actual_va);
  return 0;
}
///////////////////////////////

//
// maybe, the simplest implementation of malloc in the world ... added @lab2_2
//
uint64 sys_user_allocate_page() {
  int hid = read_tp();
  void* pa = alloc_page();
  uint64 va;
  // if there are previously reclaimed pages, use them first (this does not change the
  // size of the heap)
  if (current[hid]->user_heap.free_pages_count > 0) {
    va =  current[hid]->user_heap.free_pages_address[--current[hid]->user_heap.free_pages_count];
    assert(va < current[hid]->user_heap.heap_top);
  } else {
    // otherwise, allocate a new page (this increases the size of the heap by one page)
    va = current[hid]->user_heap.heap_top;
    current[hid]->user_heap.heap_top += PGSIZE;

    current[hid]->mapped_info[HEAP_SEGMENT].npages++;
  }
  user_vm_map((pagetable_t)current[hid]->pagetable, va, PGSIZE, (uint64)pa,
         prot_to_type(PROT_WRITE | PROT_READ, 1));

  return va;
}

//
// reclaim a page, indicated by "va". added @lab2_2
//
uint64 sys_user_free_page(uint64 va) {
  int hid = read_tp();
  user_vm_unmap((pagetable_t)current[hid]->pagetable, va, PGSIZE, 1);
  // add the reclaimed page to the free page list
  current[hid]->user_heap.free_pages_address[current[hid]->user_heap.free_pages_count++] = va;
  return 0;
}

//
// kerenl entry point of naive_fork
//
ssize_t sys_user_fork() {
  int hid = read_tp();
  sprint("hartid = %d: User call fork.\n", hid);
  return do_fork( current[hid] );
}

//
// kerenl entry point of yield. added @lab3_2
//
ssize_t sys_user_yield() {
  int hid = read_tp();
  // TODO (lab3_2): implment the syscall of yield.
  // hint: the functionality of yield is to give up the processor. therefore,
  // we should set the status of current[hid]ly running process to READY, insert it in
  // the rear of ready queue, and finally, schedule a READY process to run.
  // panic( "You need to implement the yield syscall in lab3_2.\n" );
  current[hid]->status = READY;
  insert_to_ready_queue(current[hid]);
  schedule();
  return 0;
}


// added @ lab3_challenge2
/* semaphore data structures and operations */
typedef int semaphore_t;
semaphore_t semaphores[ MAX_SEMAPHORES ]; // -1 indicates unused semaphore
int first_use = 0;
process* queue[ MAX_SEMAPHORES ][ 10 ]; // 每个信号量对应的阻塞队列
int queue_lengths[ MAX_SEMAPHORES ] = {0}; // 每个信号量对应的阻塞队列长度

// spinlock protecting semaphore data structures
static volatile int g_sem_lock = 0;

static inline void sem_lock() {
  int tmp;
  do {
    asm volatile("amoswap.w %0, %1, (%2)" : "=r"(tmp) : "r"(1), "r"(&g_sem_lock) : "memory");
  } while (tmp != 0);
}

static inline void sem_unlock() {
  asm volatile("amoswap.w x0, %0, (%1)" : : "r"(0), "r"(&g_sem_lock) : "memory");
}

ssize_t sys_user_sem_new(int count) {
  sem_lock();
   if(first_use == 0) {
       for(int i = 0; i < MAX_SEMAPHORES; i++) {
           semaphores[i] = -1; // -1 indicates unused semaphore
       }
       first_use = 1;
   }
  for(int i = 0; i < MAX_SEMAPHORES; i++) {
      if(semaphores[i] == -1) {
          semaphores[i] = count;
          sem_unlock();
          return i; // return semaphore id
      }
  }
  sem_unlock();
  return -1; // no available semaphore
}

int sys_user_sem_P(int sem) {
  int hid = read_tp();
  sem_lock();
  if (sem < 0 || sem >= MAX_SEMAPHORES || semaphores[sem] == -1) {
    sem_unlock();
    return -1; // invalid semaphore
  }

  if (semaphores[sem] > 0) {
    semaphores[sem]--;
    sem_unlock();
    return 1;
  }

  current[hid]->status = BLOCKED;

  // Avoid duplicate enqueue if the process is already waiting on this semaphore.
  int unique = 1;
  for (int i = 0; i < queue_lengths[sem]; i++) {
    if (queue[sem][i] == current[hid]) {
      unique = 0;
      break;
    }
  }

  if (unique) {
    if (queue_lengths[sem] >= 10) {
      sem_unlock();
      panic("sys_user_sem_P: wait queue overflow on sem %d\n", sem);
    }
    queue[sem][queue_lengths[sem]++] = current[hid];
  }

  // schedule() does not return to this blocked syscall frame.
  // Rewind EPC so user mode retries the P operation after wake-up.
  current[hid]->trapframe->epc -= 4;
  sem_unlock();
  schedule();
  return 0;
}

int sys_user_sem_V(int sem) {
  sem_lock();
  if (sem < 0 || sem >= MAX_SEMAPHORES || semaphores[sem] == -1) {
    sem_unlock();
    return -1; // invalid semaphore
  }

  semaphores[sem]++;
  if (queue_lengths[sem] == 0) {
    sem_unlock();
    return 1; // no process to wake up
  }

  process* process_to_wake = queue[sem][0];
  for (int i = 1; i < queue_lengths[sem]; i++) {
    queue[sem][i - 1] = queue[sem][i];
  }
  queue_lengths[sem]--;
  queue[sem][queue_lengths[sem]] = 0;
  sem_unlock();

  if (process_to_wake)
    insert_to_ready_queue(process_to_wake);
  return 1;
}

///////////////////////////////////////////////

// added @lab3_challenge3
ssize_t sys_user_printpa(uint64 va)
{
  int hid = read_tp();
  uint64 pa = (uint64)user_va_to_pa((pagetable_t)(current[hid]->pagetable), (void*)va);
  sprint("hartid = %d: pa = %lx\n", hid, pa);
  return 0;
}

///////////////////////////////////////////////

//
// open file
//
ssize_t sys_user_open(char *pathva, int flags) {
  int hid = read_tp();
  char* pathpa = (char*)user_va_to_pa((pagetable_t)(current[hid]->pagetable), pathva);
  return do_open(pathpa, flags, current[hid]->pfiles->cwd);
}


//
// read file
//
ssize_t sys_user_read(int fd, char *bufva, uint64 count) {
  int hid = read_tp();
  int i = 0;
  while (i < count) { // count can be greater than page size
    uint64 addr = (uint64)bufva + i;
    uint64 pa = lookup_pa((pagetable_t)current[hid]->pagetable, addr);
    uint64 off = addr - ROUNDDOWN(addr, PGSIZE);
    uint64 len = count - i < PGSIZE - off ? count - i : PGSIZE - off;
    uint64 r = do_read(fd, (char *)pa + off, len);
    i += r; if (r < len) return i;
  }
  return count;
}

//
// write file
//
ssize_t sys_user_write(int fd, char *bufva, uint64 count) {
  int hid = read_tp();
  int i = 0;
  while (i < count) { // count can be greater than page size
    uint64 addr = (uint64)bufva + i;
    uint64 pa = lookup_pa((pagetable_t)current[hid]->pagetable, addr);
    uint64 off = addr - ROUNDDOWN(addr, PGSIZE);
    uint64 len = count - i < PGSIZE - off ? count - i : PGSIZE - off;
    uint64 r = do_write(fd, (char *)pa + off, len);
    i += r; if (r < len) return i;
  }
  return count;
}

//
// lseek file
//
ssize_t sys_user_lseek(int fd, int offset, int whence) {
  return do_lseek(fd, offset, whence);
}

//
// read vinode
//
ssize_t sys_user_stat(int fd, struct istat *istat) {
  int hid = read_tp();
  struct istat * pistat = (struct istat *)user_va_to_pa((pagetable_t)(current[hid]->pagetable), istat);
  return do_stat(fd, pistat);
}

//
// read disk inode
//
ssize_t sys_user_disk_stat(int fd, struct istat *istat) {
  int hid = read_tp();
  struct istat * pistat = (struct istat *)user_va_to_pa((pagetable_t)(current[hid]->pagetable), istat);
  return do_disk_stat(fd, pistat);
}

//
// close file
//
ssize_t sys_user_close(int fd) {
  return do_close(fd);
}

//
// lib call to opendir
//
ssize_t sys_user_opendir(char * pathva){
  int hid = read_tp();
  char * pathpa = (char*)user_va_to_pa((pagetable_t)(current[hid]->pagetable), pathva);
  return do_opendir(pathpa, current[hid]->pfiles->cwd);
}

//
// lib call to readdir
//
ssize_t sys_user_readdir(int fd, struct dir *vdir){
  int hid = read_tp();
  struct dir * pdir = (struct dir *)user_va_to_pa((pagetable_t)(current[hid]->pagetable), vdir);
  return do_readdir(fd, pdir);
}

//
// lib call to mkdir
//
ssize_t sys_user_mkdir(char * pathva){
  int hid = read_tp();
  char * pathpa = (char*)user_va_to_pa((pagetable_t)(current[hid]->pagetable), pathva);
  return do_mkdir(pathpa);
}

//
// lib call to closedir
//
ssize_t sys_user_closedir(int fd){
  return do_closedir(fd);
}

//
// lib call to link
//
ssize_t sys_user_link(char * vfn1, char * vfn2){
  int hid = read_tp();
  char * pfn1 = (char*)user_va_to_pa((pagetable_t)(current[hid]->pagetable), (void*)vfn1);
  char * pfn2 = (char*)user_va_to_pa((pagetable_t)(current[hid]->pagetable), (void*)vfn2);
  return do_link(pfn1, pfn2);
}

//
// lib call to unlink
//
ssize_t sys_user_unlink(char * vfn){
  int hid = read_tp();
  char * pfn = (char*)user_va_to_pa((pagetable_t)(current[hid]->pagetable), (void*)vfn);
  return do_unlink(pfn);
}

// lib call to read / change current[hid] working directory @lab4_challenge1
ssize_t sys_user_rcwd(char * pathva){
  int hid = read_tp();
  char * pathpa = (char*)user_va_to_pa((pagetable_t)(current[hid]->pagetable), pathva);
  return do_rcwd(pathpa);
}

ssize_t sys_user_ccwd(char * pathva){
  int hid = read_tp();
  char * pathpa = (char*)user_va_to_pa((pagetable_t)(current[hid]->pagetable), pathva);
  return do_ccwd(pathpa, &(current[hid]->pfiles->cwd));
}

//
// implement the SYS_user_exec syscall @lab4_challenge2
//
ssize_t sys_user_exec(char *pathname, char *argv) {
  int hid = read_tp();
  // pathname 是用户空间地址，需要转换为物理地址
  char *pa_pathname = (char*)user_va_to_pa((pagetable_t)(current[hid]->pagetable), pathname);
  
  // argv 也需要转换（如果不为空）
  char *pa_argv = NULL;
  if (argv != NULL) {
    pa_argv = (char*)user_va_to_pa((pagetable_t)(current[hid]->pagetable), argv);
  }
  
  // 调用内核辅助函数执行 exec
  return do_exec(current[hid], pa_pathname, pa_argv);
}

ssize_t sys_user_wait(int pid) {
  return do_wait(pid);
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
    // added @lab1_challenge1
    case SYS_user_print_backtrace:
      return sys_user_print_backtrace(a1);
    // added @lab2_2
    case SYS_user_allocate_page:
      return sys_user_allocate_page();
    case SYS_user_free_page:
      return sys_user_free_page(a1);
    case SYS_user_fork:
      return sys_user_fork();
    case SYS_user_yield:
      return sys_user_yield();
    // added @lab2_challenge2
    case SYS_user_allocate_for_better_malloc:
      return sys_user_allocate_for_better_malloc(a1);
    case SYS_user_free_for_better_free:
      return sys_user_free_for_better_free(a1);
    // added @lab3_challenge2
    case SYS_user_sem_new:
      return sys_user_sem_new(a1);
    case SYS_user_sem_P:
      return sys_user_sem_P(a1);
    case SYS_user_sem_V:
      return sys_user_sem_V(a1);
    // added @lab3_challenge3
    case SYS_user_printpa:
      return sys_user_printpa(a1);
    // added @lab4_1
    case SYS_user_open:
      return sys_user_open((char *)a1, a2);
    case SYS_user_read:
      return sys_user_read(a1, (char *)a2, a3);
    case SYS_user_write:
      return sys_user_write(a1, (char *)a2, a3);
    case SYS_user_lseek:
      return sys_user_lseek(a1, a2, a3);
    case SYS_user_stat:
      return sys_user_stat(a1, (struct istat *)a2);
    case SYS_user_disk_stat:
      return sys_user_disk_stat(a1, (struct istat *)a2);
    case SYS_user_close:
      return sys_user_close(a1);
    // added @lab4_2
    case SYS_user_opendir:
      return sys_user_opendir((char *)a1);
    case SYS_user_readdir:
      return sys_user_readdir(a1, (struct dir *)a2);
    case SYS_user_mkdir:
      return sys_user_mkdir((char *)a1);
    case SYS_user_closedir:
      return sys_user_closedir(a1);
    // added @lab4_3
    case SYS_user_link:
      return sys_user_link((char *)a1, (char *)a2);
    case SYS_user_unlink:
      return sys_user_unlink((char *)a1);
    // added @lab4_challenge1
    case SYS_user_ccwd:
      return sys_user_ccwd((char *)a1);
    case SYS_user_rcwd:
      return sys_user_rcwd((char *)a1);
    // added @lab4_challenge2
    case SYS_user_exec:
      return sys_user_exec((char *)a1, (char *)a2);
    case SYS_user_wait:
       return sys_user_wait(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
