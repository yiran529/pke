# 1
 以最简单的app_helloworld.c为例，我们先来分析一下它的结构。
# 2
 首先是如何进入这个main的，lab1暂时不关心
# 3
 接着看它如何进行系统调用（https://gitee.com/hustos/pke-doc/blob/master/chapter3_traps.md#给定应用 也有介绍）：

当用户程序执行`ecall`指令时，系统会经历以下步骤：

## 1. 用户空间触发系统调用

用户程序通过`do_user_call`函数触发系统调用：

```c
int do_user_call(uint64 sysnum, uint64 a1, uint64 a2, uint64 a3, uint64 a4, uint64 a5, uint64 a6,
                 uint64 a7) {
  int ret;
  asm volatile(
      "ecall\n"      // 触发系统调用
      "sw a0, %0"    // 保存返回值
      : "=m"(ret)
      :
      : "memory");
  return ret;
}
```

在调用此函数时，根据RISC-V调用约定，参数会自动加载到寄存器中：
- sysnum → a0 (系统调用号)
- a1 → a1 (第一个参数)
- a2 → a2 (第二个参数)
- ...以此类推

## 2. 处理器模式切换

执行`ecall`指令后：
1. 处理器从用户态(UMODE)切换到监管态(SMODE)
2. 硬件自动跳转到`stvec`寄存器指向的trap处理向量(`smode_trap_vector`)

## 3. 上下文保存

在`smode_trap_vector`中(位于strap_vector.S)：
1. 保存用户进程的所有寄存器上下文到trapframe中
2. 切换到内核栈
3. 跳转到C语言编写的trap处理函数[smode_trap_handler](file:///app/riscv-pke/kernel/strap.h#L3-L3)

## 4. 内核trap处理

在[smode_trap_handler](file:///app/riscv-pke/kernel/strap.h#L3-L3)函数中(strap.c)：
1. 检查trap来源是否为用户态
2. 读取`scause`寄存器判断trap原因
3. 如果是用户ecall([CAUSE_USER_ECALL](file:///app/riscv-pke/kernel/riscv.h#L47-L48))，则调用`handle_syscall`

```c
void smode_trap_handler(void) {
  if ((read_csr(sstatus) & SSTATUS_SPP) != 0) panic("usertrap: not from user mode");
  
  assert(current);
  current->trapframe->epc = read_csr(sepc);

  if (read_csr(scause) == CAUSE_USER_ECALL) {
    handle_syscall(current->trapframe);
  } else {
    // 处理其他异常
    panic("unexpected exception happened.\n");
  }
}
```

## 5. 系统调用分发

在`handle_syscall`函数中：
1. 更新程序计数器指向下一条指令
2. 调用[do_syscall](file:///app/riscv-pke/kernel/syscall.h#L11-L11)进行具体处理

```c
static void handle_syscall(trapframe *tf) {
  tf->epc += 4; // 指向下一条指令
  // 实际实现应该调用do_syscall并将返回值存入tf->regs.a0
}
```

## 6. 系统调用执行

`do_syscall`函数(syscall.c)根据系统调用号分发到具体处理函数：
```c
long do_syscall(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
  switch (a0) {
    case SYS_user_print:
      return sys_user_print((const char*)a1, a2);
    case SYS_user_exit:
      return sys_user_exit(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
```

## 7. 返回用户空间

处理完成后：
1. 通过[switch_to](file:///app/riscv-pke/kernel/process.h#L25-L25)函数返回用户态
2. 恢复用户进程的寄存器上下文
3. 从`sepc`寄存器指定的地址继续执行用户程序

整个流程体现了操作系统内核如何安全地处理来自用户程序的请求，同时保护系统资源不被非法访问。这种机制是现代操作系统的核心功能之一。

## a0的作用
a0寄存器在系统调用中有三重作用：
1. 传递系统调用号：在系统调用开始时，a0包含系统调用号
2. 传递参数：a0本身就是系统调用的第一个参数
3. 返回值传递：系统调用完成后，返回值通过a0寄存器传递回用户程序

## trapframe初始化
load_user_program, switch_to
trapframe属于内核管理的数据结构，但用于保存特定用户进程的上下文信息(用于恢复用户进程的寄存器上下文)