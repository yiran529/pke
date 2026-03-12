# 在我们的OS中多核CPU会有什么限制？
主要是多进程和文件系统的兼容问题
1. 同一个进程能不能同时在多个核上跑？
2. 引用计数要不要防止race？
3. alloc_process要上锁？
4. 每个process自带一个锁？
5. 每个pte上一个锁？ 不用
5. ready_queue_head


为了方便实现，统统都是一把大锁保平安。

# 进一步debug遇到的问题
1. 原来设计判定目录的方式是无法用“读”权限打开，这会导致与文件不存在的错误路径冲突。当文件是存在，没有问题时就不会出事；但是当文件不存在就可能会给出不合理的错误提示
2. 当一个核跑完了，而且等待队列中没有在等待的进程，而另一个核还在跑时，这个核需要wfi等到另一个核跑完，而不是一直递归schedule等，因为这会大量挤占资源

3. 多核下 `tp`（hartid）会被 trapframe 覆盖，导致后续 `read_tp()` 读错核号  
   - 触发链路：  
     1) 进程先在 hart A 上运行，`trapframe->regs.tp = A`；  
     2) 调度迁移到 hart B 后，如果不更新 trapframe 的 tp；  
     3) 返回用户态时 `restore_all_registers` 会把旧 tp(A) 写回寄存器；  
     4) 下一次陷入内核后，`read_tp()` 仍读到 A，错误索引 `current[A]`，造成 syscall/调度上下文错乱。  
   - 现象：`Unknown syscall`、`unexpected scause`、`sepc=0` 等随机崩溃。  
   - 修复：在 `switch_to()` 中每次调度都同步 `proc->trapframe->regs.tp = hid`。  

4. 信号量与 wait 的阻塞语义需要和调度流一致  
   - `sem_P` 阻塞后应 `epc -= 4`，唤醒后重试同一条 P 指令；  
   - `waiting_pid` 需要区分“未等待”和“wait(-1)”两种状态，避免 `exit` 误唤醒父进程；  
   - `wait` 返回前需要回收 ZOMBIE 子进程槽位，否则双 shell 压测会耗尽 `NPROC`。  
