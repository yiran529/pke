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
  
  // sprint("\n=== KERNEL MODE: Manual Translation Required ===\n");
  // sprint("buf (user VA): 0x%lx\n", (uint64)buf);
  // sprint("Current satp:  0x%lx (points to KERNEL page table)\n", read_csr(satp));
  // sprint("User pagetable: 0x%lx\n", (uint64)current->pagetable);
  // sprint("Why manual? satp != user_pagetable, so MMU can't auto-translate buf!\n");
  // sprint("Calling user_va_to_pa() to manually walk user page table...\n");
  
  char* pa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)buf);
  sprint(pa);
  // sprint("Translated PA: 0x%lx\n", (uint64)pa);
  // sprint("Message: %s", pa);
  // sprint("===========================================\n\n");
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
    if (last_chunk_va + hdr->size >= heap_end) {
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
    last_chunk_va += hdr->size;
  }
  
  p->heap_size += expand_bytes;
  return old_heap_end;
}

uint64 find_first_fit(process *p, uint64 size) {
  uint64 va = p->heap_va;
  uint64 heap_limit = p->heap_va + p->heap_size; // 使用heap_size支持多页
  while (va + CHUNK_HDR_SIZE <= heap_limit) {
    // VA -> PA 再访问
    heap_chunk_t *hdr = (heap_chunk_t *)user_va_to_pa(p->pagetable, (void*)va);
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

//
// maybe, the simplest implementation of malloc in the world ... added @lab2_2
//
uint64 sys_user_allocate_page(int n) {
  // void* pa = alloc_page();
  // /*取当前“用户简单堆”指针的当前位置作为本次分配的虚拟页起始地址。
  // g_ufree_page 是一个单调递增游标，表示下一个可用的用户虚拟地址（位于用户进程的“自由区”起点之后）*/
  // uint64 va = g_ufree_page;
  // g_ufree_page += PGSIZE;
  // user_vm_map((pagetable_t)current->pagetable, va, PGSIZE, (uint64)pa,
  //        prot_to_type(PROT_WRITE | PROT_READ, 1));
  uint64 va = find_first_fit(current, n);
  
  if (va == 0) {
    // 没有合适的空闲块，尝试扩展堆
    uint64 needed = ALIGN_UP(n + CHUNK_HDR_SIZE, CHUNK_ALIGN);
    if (expand_heap(current, needed) == 0) {
      // 扩展失败
      panic("Failed to expand heap or out of memory!\n");
    }
    
    // 重新尝试分配
    va = find_first_fit(current, n);
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
uint64 sys_user_free_page(uint64 va) {
  // TODO : 如果va是非法的???????
  uint64 actual_va = va - CHUNK_HDR_SIZE;
  heap_chunk_t *hdr = (heap_chunk_t *)user_va_to_pa(current->pagetable, (void*)actual_va);
  hdr->flags = 0; // 标记为 free
  // 向前向后合并空闲块
  collesce_forward(current, hdr, actual_va);
  collesce_backward(current, hdr, actual_va);
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
      return sys_user_allocate_page(a1);
    case SYS_user_free_page:
      return sys_user_free_page(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
