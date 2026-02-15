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
  if(current->parent) {current->parent->status = READY; }
  if(current->parent) {insert_to_ready_queue( current->parent ); }
  // reclaim the current process, and reschedule. added @lab3_1
  free_process( current );
  schedule();
  return 0;
}

//
// maybe, the simplest implementation of malloc in the world ... added @lab2_2
//
uint64 sys_user_allocate_page() {
  void* pa = alloc_page();
  uint64 va;
  // if there are previously reclaimed pages, use them first (this does not change the
  // size of the heap)
  if (current->user_heap.free_pages_count > 0) {
    va =  current->user_heap.free_pages_address[--current->user_heap.free_pages_count];
    assert(va < current->user_heap.heap_top);
  } else {
    // otherwise, allocate a new page (this increases the size of the heap by one page)
    va = current->user_heap.heap_top;
    current->user_heap.heap_top += PGSIZE;

    current->mapped_info[HEAP_SEGMENT].npages++;
  }
  user_vm_map((pagetable_t)current->pagetable, va, PGSIZE, (uint64)pa,
         prot_to_type(PROT_WRITE | PROT_READ, 1));

  return va;
}

//
// reclaim a page, indicated by "va". added @lab2_2
//
uint64 sys_user_free_page(uint64 va) {
  user_vm_unmap((pagetable_t)current->pagetable, va, PGSIZE, 1);
  // add the reclaimed page to the free page list
  current->user_heap.free_pages_address[current->user_heap.free_pages_count++] = va;
  return 0;
}

//
// kerenl entry point of naive_fork
//
ssize_t sys_user_fork() {
  sprint("User call fork.\n");
  return do_fork( current );
}

//
// kerenl entry point of yield. added @lab3_2
//
ssize_t sys_user_yield() {
  // TODO (lab3_2): implment the syscall of yield.
  // hint: the functionality of yield is to give up the processor. therefore,
  // we should set the status of currently running process to READY, insert it in
  // the rear of ready queue, and finally, schedule a READY process to run.
  // panic( "You need to implement the yield syscall in lab3_2.\n" );
  current->status = READY;
  insert_to_ready_queue(current);
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

ssize_t sys_user_sem_new(int count) {
   if(first_use == 0) {
       for(int i = 0; i < MAX_SEMAPHORES; i++) {
           semaphores[i] = -1; // -1 indicates unused semaphore
       }
       first_use = 1;
   }
  for(int i = 0; i < MAX_SEMAPHORES; i++) {
      if(semaphores[i] == -1) {
          semaphores[i] = count;
          return i; // return semaphore id
      }
  }
  return -1; // no available semaphore
}

int sys_user_sem_P(int sem) {
    if(sem < 0 || sem >= MAX_SEMAPHORES || semaphores[sem] == -1) {
        return -1; // invalid semaphore
    }

    // sprint("[DEBUG] P operation on semaphore %d, val = %d\n", sem, semaphores[sem]);
    while(1) {
      if(semaphores[sem] > 0) {
        semaphores[sem]--;
        return 1;
      } else {
        // sprint("[DEBUG] Semaphore %d is not available, blocking current process %d\n", sem, current->pid);
        // insert into semaphore's blocked queue
        // block the current process
        current -> status = BLOCKED;

        // make sure the process is not already in the blocked queue
        int unique = 1;
        for(int i = 0; i < queue_lengths[sem]; i++) {
          if (queue[sem][i] == current) {            
            unique = 0;
          }
        }

        if (unique) {
          queue[sem][queue_lengths[sem]++] = current; 
        }

        current->trapframe->epc -= 4; // 让被阻塞的进程在恢复时重新执行P操作
        schedule();
      }
    }
    
    return 0;
}

int sys_user_sem_V(int sem) {
    if(sem < 0 || sem >= MAX_SEMAPHORES || semaphores[sem] == -1) {
        return -1; // invalid semaphore
    }

    // sprint("[DEBUG] V operation on semaphore %d, val = %d\n", sem, semaphores[sem]);
    semaphores[sem]++;
    // unblock a process from semaphore's blocked queue

    if(queue_lengths[sem] == 0) {
        return 1; // no process to wake up
    }

    process* process_to_wake = queue[sem][0];
    if (process_to_wake == NULL) {
        return 1; 
    }

    // sprint("[DEBUG] Waking up process %d from semaphore %d's blocked queue\n", process_to_wake->pid, sem);
    // shift the queue //TODO 可以考虑使用循环队列稍微提升性能
    for(int i = 1; i < queue_lengths[sem]; i++) {
        queue[sem][i - 1] = queue[sem][i];
    }
    queue_lengths[sem]--;
    // sprint("[DEBUG] Semaphore %d blocked queue length: %d\n", sem, queue_lengths[sem]);
    // find the process and set it to READY
    insert_to_ready_queue(process_to_wake);
    return 1;
}

///////////////////////////////////////////////

// added @lab3_challenge3
ssize_t sys_user_printpa(uint64 va)
{
  uint64 pa = (uint64)user_va_to_pa((pagetable_t)(current->pagetable), (void*)va);
  sprint("%lx\n", pa);
  return 0;
}

///////////////////////////////////////////////

//
// open file
//
ssize_t sys_user_open(char *pathva, int flags) {
  char* pathpa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), pathva);
  return do_open(pathpa, flags, current->pfiles->cwd);
}


//
// read file
//
ssize_t sys_user_read(int fd, char *bufva, uint64 count) {
  int i = 0;
  while (i < count) { // count can be greater than page size
    uint64 addr = (uint64)bufva + i;
    uint64 pa = lookup_pa((pagetable_t)current->pagetable, addr);
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
  int i = 0;
  while (i < count) { // count can be greater than page size
    uint64 addr = (uint64)bufva + i;
    uint64 pa = lookup_pa((pagetable_t)current->pagetable, addr);
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
  struct istat * pistat = (struct istat *)user_va_to_pa((pagetable_t)(current->pagetable), istat);
  return do_stat(fd, pistat);
}

//
// read disk inode
//
ssize_t sys_user_disk_stat(int fd, struct istat *istat) {
  struct istat * pistat = (struct istat *)user_va_to_pa((pagetable_t)(current->pagetable), istat);
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
  char * pathpa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), pathva);
  return do_opendir(pathpa, current->pfiles->cwd);
}

//
// lib call to readdir
//
ssize_t sys_user_readdir(int fd, struct dir *vdir){
  struct dir * pdir = (struct dir *)user_va_to_pa((pagetable_t)(current->pagetable), vdir);
  return do_readdir(fd, pdir);
}

//
// lib call to mkdir
//
ssize_t sys_user_mkdir(char * pathva){
  char * pathpa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), pathva);
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
  char * pfn1 = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)vfn1);
  char * pfn2 = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)vfn2);
  return do_link(pfn1, pfn2);
}

//
// lib call to unlink
//
ssize_t sys_user_unlink(char * vfn){
  char * pfn = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)vfn);
  return do_unlink(pfn);
}

// lib call to read / change current working directory @lab4_challenge1
ssize_t sys_user_rcwd(char * pathva){
  char * pathpa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), pathva);
  return do_rcwd(pathpa);
}

ssize_t sys_user_ccwd(char * pathva){
  char * pathpa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), pathva);
  return do_ccwd(pathpa, &(current->pfiles->cwd));
}

//
// implement the SYS_user_exec syscall @lab4_challenge2
//
ssize_t sys_user_exec(char *pathname, char *argv) {
  // pathname 是用户空间地址，需要转换为物理地址
  char *pa_pathname = (char*)user_va_to_pa((pagetable_t)(current->pagetable), pathname);
  
  // argv 也需要转换（如果不为空）
  char *pa_argv = NULL;
  if (argv != NULL) {
    pa_argv = (char*)user_va_to_pa((pagetable_t)(current->pagetable), argv);
  }
  
  // 调用内核辅助函数执行 exec
  return do_exec(current, pa_pathname, pa_argv);
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
    // added @lab2_2
    case SYS_user_allocate_page:
      return sys_user_allocate_page();
    case SYS_user_free_page:
      return sys_user_free_page(a1);
    case SYS_user_fork:
      return sys_user_fork();
    case SYS_user_yield:
      return sys_user_yield();
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
