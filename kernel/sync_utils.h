#ifndef _SYNC_UTILS_H_
#define _SYNC_UTILS_H_

/*
 * sync_barrier: a simple spin barrier for bare-metal multicore bring-up.
 * - Each hart atomically increments the shared counter once; the returned old value is
 *   stored in `local`. When the last hart arrives, local+1 == all.
 * - Harts arriving earlier will spin, reloading the counter until it reaches `all`,
 *   which means every hart has passed the barrier.
 *
 * Why needed for双核: HTIF/DTB初始化等资源只允许hart0执行一次，其它hart必须等
 * 待完成后再访问共享资源；使用该屏障即可在M态同步两核的启动阶段。
 */
static inline void sync_barrier(volatile int *counter, int all) {

  int local;

  asm volatile("amoadd.w %0, %2, (%1)\n"
               : "=r"(local)
               : "r"(counter), "r"(1)
               : "memory");

  if (local + 1 < all) {
    do {
      asm volatile("lw %0, (%1)\n" : "=r"(local) : "r"(counter) : "memory");
    } while (local < all);
  }
}

#endif