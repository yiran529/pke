/*
 * implementing the scheduler
 */

#include "sched.h"
#include "spike_interface/spike_utils.h"

process* ready_queue_head = NULL;

// spinlock protecting the ready queue
static volatile int g_sched_lock = 0;

static inline void sched_lock() {
  int tmp;
  do {
    asm volatile("amoswap.w %0, %1, (%2)" : "=r"(tmp) : "r"(1), "r"(&g_sched_lock) : "memory");
  } while (tmp != 0);
}

static inline void sched_unlock() {
  asm volatile("amoswap.w x0, %0, (%1)" : : "r"(0), "r"(&g_sched_lock) : "memory");
}

//
// insert a process, proc, into the END of ready queue.
//
void insert_to_ready_queue( process* proc ) {
  sched_lock();
  sprint( "going to insert process %d to ready queue.\n", proc->pid );
  // if the queue is empty in the beginning
  if( ready_queue_head == NULL ){
    proc->status = READY;
    proc->queue_next = NULL;
    ready_queue_head = proc;
    sched_unlock();
    return;
  }

  // ready queue is not empty
  process *p;
  // browse the ready queue to see if proc is already in-queue
  for( p=ready_queue_head; p->queue_next!=NULL; p=p->queue_next )
    if( p == proc ) { sched_unlock(); return; }  //already in queue

  // p points to the last element of the ready queue
  if( p==proc ) { sched_unlock(); return; }
  p->queue_next = proc;
  proc->status = READY;
  proc->queue_next = NULL;

  sched_unlock();
  return;
}

//
// choose a proc from the ready queue, and put it to run.
// note: schedule() does not take care of previous current process. If the current
// process is still runnable, you should place it into the ready queue (by calling
// ready_queue_insert), and then call schedule().
//
extern process procs[NPROC];
void schedule() {
  int hid = read_tp();
  sprint( "hartid = %d: will schedule a process to run. (current: %d, state: %d)\n", hid, current[hid] ? current[hid]->pid : -1, current[hid] ? current[hid]->status : -1 );

  sched_lock();
  if ( !ready_queue_head ){
    // by default, if there are no ready process, and all processes are in the status of
    // FREE and ZOMBIE, we should shutdown the emulated RISC-V machine.
    // 下面检查是否应该shutdown
    // 如果所有进程都处于FREE或ZOMBIE状态，则shutdown；否则，说明还有未完成的进程，我们应该让系统等待它们完成，而不是直接shutdown。
    int should_shutdown = 1;

    for( int i=0; i<NPROC; i++ )
      if( (procs[i].status != FREE) && (procs[i].status != ZOMBIE) ){
        should_shutdown = 0;
        sprint( "hartid = %d: ready queue empty, but process %d is not in free/zombie state:%d\n", 
          hid, i, procs[i].status );
      }

    if( should_shutdown ){
      sched_unlock();
      sprint( "hartid = %d: no more ready processes, system shutdown now.\n", hid );
      shutdown( 0 );
    }else{
      // 释放锁，然后使用WFI指令等待中断（如时钟中断或其他hart插入进程到就绪队列）
      sched_unlock();
      sprint( "hartid = %d: no ready processes, waiting for unfinished processes.\n", hid );
      // 为什么要wfi: 避免空转浪费CPU，让hart进入低功耗状态
      // 什么终止wfi: 任何中断（如时钟中断、其他hart的IPI中断等）都会唤醒wfi
      // 为什么不能递归调用schedule: 
      //   1. 栈溢出风险(每个hart只有4KB内核栈)，可能破坏共享的全局数据结构
      //   2. 频繁抢锁导致其他hart获取调度锁困难
      //   3. 大量sprint调用竞争共享的HTIF控制台输出
      asm volatile("wfi");
      // 被中断唤醒后，重新尝试调度
      schedule();
      return;
    }
  }

  current[hid] = ready_queue_head;
  assert( current[hid]->status == READY );
  ready_queue_head = ready_queue_head->queue_next;

  current[hid]->status = RUNNING;
  sched_unlock();

  sprint( "going to schedule process %d to run.\n", current[hid]->pid );
  switch_to( current[hid] );
}
