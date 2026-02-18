#include "pmm.h"
#include "util/functions.h"
#include "riscv.h"
#include "config.h"
#include "util/string.h"
#include "memlayout.h"
#include "spike_interface/spike_utils.h"

// _end is defined in kernel/kernel.lds, it marks the ending (virtual) address of PKE kernel
extern char _end[];
// g_mem_size is defined in spike_interface/spike_memory.c, it indicates the size of our
// (emulated) spike machine. g_mem_size's value is obtained when initializing HTIF. 
extern uint64 g_mem_size;

static uint64 free_mem_start_addr;  //beginning address of free memory
static uint64 free_mem_end_addr;    //end address of free memory (not included)
static uint64 page_base; // base physical address for page frame number 0(right after ROUNDUP(g_kernel_end , PGSIZE))

typedef struct node {
  struct node *next;
} list_node;

// g_free_mem_list is the head of the list of free physical memory pages
static list_node g_free_mem_list;
// spinlock protecting the free list
static volatile int g_pmm_lock = 0;

static inline void pmm_lock() {
  int tmp;
  do {
    asm volatile("amoswap.w %0, %1, (%2)" : "=r"(tmp) : "r"(1), "r"(&g_pmm_lock) : "memory");
  } while (tmp != 0);
}

static inline void pmm_unlock() {
  asm volatile("amoswap.w x0, %0, (%1)" : : "r"(0), "r"(&g_pmm_lock) : "memory");
}

// used for reference counting of physical pages to implement copy-on-write
// 全局引用计数数组 & 元数据
static uint32 *page_refcount = 0; // 指向 refcount 数组
static uint64 nphys_pages = 0;    // 物理页总数
static uint64 refcount_pages = 0; // 用于存放数组所占页数

// kernel heap: contiguous physical memory reserved before free page list creation
#define KERNEL_HEAP_PAGES 1024  // 4MB kernel heap
static uint64 kheap_start;
static uint64 kheap_current;
static uint64 kheap_end;

int vm_alloc_stage[NCPU] = { 0 }; // 0 for kernel alloc, 1 for user alloc


//
// actually creates the freepage list. each page occupies 4KB (PGSIZE), i.e., small page.
// PGSIZE is defined in kernel/riscv.h, ROUNDUP is defined in util/functions.h.
//
static void create_freepage_list(uint64 start, uint64 end) {
  g_free_mem_list.next = 0;
  for (uint64 p = ROUNDUP(start, PGSIZE); p + PGSIZE < end; p += PGSIZE)
    free_page( (void *)p );
}

//
// place a physical page at *pa to the free list of g_free_mem_list (to reclaim the page)
//
void free_page(void *pa) {
  if (((uint64)pa % PGSIZE) != 0 || (uint64)pa < free_mem_start_addr || (uint64)pa >= free_mem_end_addr)
    panic("free_page 0x%lx \n", pa);

  pmm_lock();
  // insert a physical page to g_free_mem_list
  list_node *n = (list_node *)pa;
  n->next = g_free_mem_list.next;
  g_free_mem_list.next = n;
  pmm_unlock();
}

//
// takes the first free page from g_free_mem_list, and returns (allocates) it.
// Allocates only ONE page!
//
void *alloc_page(void) {
  pmm_lock();
  list_node *n = g_free_mem_list.next;

  uint64 hartid = read_tp();
  if (vm_alloc_stage[hartid]) {
    sprint("hartid = %ld: alloc page 0x%x\n", hartid, n);
  }
  
  if (n) g_free_mem_list.next = n->next;

  inc_page_refcount((uint64)n);
  pmm_unlock();
  return (void *)n;
}

//
// pmm_init() establishes the list of free physical pages according to available
// physical memory space.
//
void pmm_init() {
  // start of kernel program segment
  uint64 g_kernel_start = KERN_BASE;
  uint64 g_kernel_end = (uint64)&_end;

  uint64 pke_kernel_size = g_kernel_end - g_kernel_start;
  // 打印内核在虚拟地址空间中的起止地址以及大小，便于调试
  sprint("PKE kernel start 0x%lx, PKE kernel end: 0x%lx, PKE kernel size: 0x%lx .\n",
    g_kernel_start, g_kernel_end, pke_kernel_size);

  // free memory starts from the end of PKE kernel and must be page-aligined
  // 将可用物理内存的起始地址设置为内核结束地址向上对齐到页面边界
  // 这样保证后续按页管理时，空闲链表中的每个条目都是整页对齐的地址
  free_mem_start_addr = ROUNDUP(g_kernel_end , PGSIZE);

  // recompute g_mem_size to limit the physical memory space that our riscv-pke kernel
  // needs to manage
  g_mem_size = MIN(PKE_MAX_ALLOWABLE_RAM, g_mem_size);
  if( g_mem_size < pke_kernel_size )
    panic( "Error when recomputing physical memory size (g_mem_size).\n" );

  // 计算可管理的物理内存结束地址（不包含），这里用 DRAM_BASE 作为物理内存基地址
  // 注意：g_mem_size 已可能被限制为 PKE_MAX_ALLOWABLE_RAM，以避免使用模拟器提供的全部内存
  free_mem_end_addr = g_mem_size + DRAM_BASE;

  // Below are initializing the page reference count array for copy-on-write
  // 1) 计算物理页总数（基于 DRAM_BASE 到 free_mem_end_addr）
  uint64 total_bytes = free_mem_end_addr - free_mem_start_addr; // 之类不需要+1，因为end_addr本身不包含在内
  sprint("free_mem_start_addr = 0x%lx, free_mem_end_addr = 0x%lx, total_bytes = %ld \n",
    free_mem_start_addr, free_mem_end_addr, total_bytes);
  nphys_pages = total_bytes / PGSIZE;

  // 2) 计算存放 refcount 数组需要的页数
  uint64 bytes_for_ref = nphys_pages * sizeof(uint32); // 数组占据的字节数
  refcount_pages = (bytes_for_ref + PGSIZE - 1) / PGSIZE; // 向上取整页数

  // 3) 将这块内存从 free pool 中保留出来（放在 free region 的起始处）
  page_refcount = (uint32 *)free_mem_start_addr;
  sprint("total_bytes = %ld, nphys_pages = %ld, setting up page refcount array at address 0x%lx, "
    "size: %ld bytes (%ld pages) \n", total_bytes, nphys_pages, free_mem_start_addr, bytes_for_ref, refcount_pages);
  // 清零（注意：此处 page_refcount 处于尚未回收为 free list 的范围）
  memset(page_refcount, 0, refcount_pages * PGSIZE);
  sprint("ok2\n");

  // 4) 移动 free_mem_start_addr，避免后续 create_freepage_list 回收这部分页
  page_base = free_mem_start_addr;
  free_mem_start_addr += refcount_pages * PGSIZE;

  sprint("finish setting up page refcount array, nphys_pages: %ld, "
    "refcount_pages: %ld \n", nphys_pages, refcount_pages);

  // 5) Reserve contiguous kernel heap for kmalloc (before free page list creation)
  kheap_start = free_mem_start_addr;
  kheap_current = kheap_start;
  kheap_end = kheap_start + KERNEL_HEAP_PAGES * PGSIZE;
  free_mem_start_addr = kheap_end;
  memset((void *)kheap_start, 0, KERNEL_HEAP_PAGES * PGSIZE);
  sprint("Kernel heap: [0x%lx, 0x%lx], size: %ld bytes\n",
    kheap_start, kheap_end, (uint64)(KERNEL_HEAP_PAGES * PGSIZE));

  // 打印空闲物理内存范围（起始地址已向上对齐为页边界，结束地址为最后可用字节）
  sprint("free physical memory address: [0x%lx, 0x%lx] \n", free_mem_start_addr,
    free_mem_end_addr - 1);
  
  sprint("kernel memory manager is initializing ...\n");
  // create the list of free pages
  // 这里创建空闲页链表：遍历 [free_mem_start_addr, free_mem_end_addr) 区间内的每一整页
  // 并把每页加入到 g_free_mem_list 链表中，供 alloc_page()/free_page() 使用。
  // 注意事项：
  // - create_freepage_list 会对每个页面调用 free_page()，因此 free_page() 中的
  //   范围检查（pa 在 free_mem_start_addr 与 free_mem_end_addr 之间）必须已生效。
  // - 如果将来需要为内核元数据（例如页引用计数数组 page_refcount）预留空间，
  //   应在调用 create_freepage_list 之前改变 free_mem_start_addr，从而保护这些页不被回收。
  create_freepage_list(free_mem_start_addr, free_mem_end_addr);
}

//
// kmalloc: simple bump allocator on the pre-reserved contiguous kernel heap.
// TODO Memory allocated by kmalloc is never freed (acceptable for PKE's debug info).
//
void *kmalloc(uint64 size) {
  // align to 8 bytes
  size = (size + 7) & ~(uint64)7;
  if (kheap_current + size > kheap_end)
    panic("kmalloc: kernel heap exhausted (requested %ld bytes, remain %ld)\n",
          size, kheap_end - kheap_current);
  void *p = (void *)kheap_current;
  kheap_current += size;
  return p;
}

// Save the current heap position (to allow bulk rollback later).
uint64 kmalloc_mark(void) {
  return kheap_current;
}

// Reset the heap pointer back to a previously saved mark.
// All kmalloc allocations since that mark are effectively freed.
void kmalloc_reset(uint64 mark) {
  if (mark >= kheap_start && mark <= kheap_end)
    kheap_current = mark;
}

// helper: convert physical address to page index (based on DRAM_BASE)
// helper: convert physical address to page index (based on page_base)
static inline uint64 pa_to_page_index(uint64 pa) {
  if (pa >= free_mem_end_addr || pa < page_base)
    return (uint64)-1; // invalid
  return (pa - page_base) / PGSIZE;
}

void inc_page_refcount(uint64 pa) {
  uint64 idx = pa_to_page_index(pa);
  if (idx == (uint64)-1 || idx >= nphys_pages) {
    panic("inc_page_refcount: invalid physical address 0x%lx\n", pa);
  }
  // 原子加 1，返回值忽略
  __sync_add_and_fetch(&page_refcount[idx], 1);
}

uint32 dec_page_refcount(uint64 pa) {
  uint64 idx = pa_to_page_index(pa);
  if (idx == (uint64)-1 || idx >= nphys_pages) {
    panic("dec_page_refcount: invalid physical address 0x%lx\n", pa);
  }
  // 原子减 1，fetch-and-sub 返回减前的值
  uint32 prev = __sync_fetch_and_sub(&page_refcount[idx], 1);
  if (prev == 0) {
    // 本来就是 0，BUG：不该减到负值
    panic("dec_page_refcount: underflow at pa 0x%lx\n", pa);
  }
  uint32 now = prev - 1;
  // 如果需要在计数减到 0 时自动释放物理页，可以在这里调用 free_page((void*)pa)
  return now;
}

uint32 get_page_refcount(uint64 pa) {
  uint64 idx = pa_to_page_index(pa);
  if (idx == (uint64)-1 || idx >= nphys_pages) {
    panic("get_page_refcount: invalid physical address 0x%lx\n", pa);
  }
  // 原子读：用 fetch-and-add 0 或其他原子读
  return __sync_add_and_fetch(&page_refcount[idx], 0);
}
