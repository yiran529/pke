Read [](file:///app/riscv-pke/util/snprintf.h)

Read [](file:///app/riscv-pke/util/string.h)

Read [](file:///app/riscv-pke/util/load_store.S)

Read [](file:///app/riscv-pke/kernel/config.h)

Read [](file:///app/riscv-pke/kernel/riscv.h)

Read [](file:///app/riscv-pke/spike_interface/spike_file.h)

Read [](file:///app/riscv-pke/spike_interface/spike_htif.h)

Read [](file:///app/riscv-pke/spike_interface/spike_memory.h)

Read [](file:///app/riscv-pke/kernel/kernel.lds)

Read [](file:///app/riscv-pke/user/user.lds)

Starting (10/10) *Summarize big picture in Chinese*

Completed (10/10) *Summarize big picture in Chinese*

下面是针对当前代码库（Lab1_1阶段）的全局 Big Picture，总结模块职责、已具备的“操作系统”能力，以及进一步的说明。  

## 模块功能概览

### 根目录与构建
- Makefile：定义四类输出目标：`util.a`（基础工具库）、`spike_interface.a`（与 Spike/HTIF 交互）、`riscv-pke`（内核 ELF）、`app_helloworld`（用户程序 ELF）。使用 `-nostdlib -fno-builtin` 最小化依赖；`-mcmodel=medany` 允许跨段取地址；`run` 通过 `spike kernel userapp` 同时加载内核和应用。
- kernel.lds / user.lds：两个独立链接脚本，分别将内核放到物理地址 `0x80000000`，用户程序放到 `0x81000000`。Bare 模式下“虚拟=物理”，直接用固定地址，贴近教学目的。
- README.md：阐述 Proxy Kernel 教学理念：按应用逐步“补完”内核，不追求完整 OS。

### 内核入口与特权模式流转（machine → supervisor → user）
- mentry.S：Spike 启动后先跳到 `_mentry`（ENTRY 指定），设置每个 HART 的栈（现在仅 `NCPU=1`），然后 `call m_start`。
- minit.c：
  - `m_start()`：初始化 HTIF/内存（解析设备树 DTB），设置 `mstatus.MPP = S`，`mepc = s_start`，委托异常与中断到 S 模式（`mideleg/medeleg`），`mret` 进入 Supervisor。
- kernel.c：
  - `s_start()`：打印进入 S 模式；关闭分页（`satp=0` Bare 模式）；调用 `load_user_program()` 装载用户 ELF；最后 `switch_to()` 转入用户态执行。

### ELF 加载与进程最小抽象
- elf.c / elf.h：通过 HTIF 前端模拟“主机文件系统”访问：解析命令行参数（获取用户程序名）、读取 ELF Header 与 Program Headers，按段加载到对应虚实相等地址。设置进程 `trapframe->epc = entry`。
- process.h / process.c：
  - `process` 仅包含：`kstack`（陷入内核使用的内核栈）、`trapframe*`（保存用户上下文）。
  - `switch_to(proc)`：设置 `stvec = smode_trap_vector`，准备返回用户态所需的 `kernel_sp` / `kernel_trap` 字段，清除 `sstatus.SPP` 使下一次 `sret` 回到 User，写入 `sepc`，调用 `return_to_user`（汇编）。

### Trap 向量与上下文保存/恢复
- strap_vector.S：
  - `smode_trap_vector`：进入 S 模式后将用户态上下文全部保存到 `trapframe.regs`（宏 `store_all_registers`）；切换到“用户进程的内核栈”；跳转到 `smode_trap_handler`。
  - `return_to_user`：恢复通用寄存器（`restore_all_registers`），`sret` 回到用户态并继续执行 `sepc`。
- load_store.S：提供批量保存/恢复寄存器的宏，配合 trapframe 布局（偏移与 `riscv_regs_t` 对齐）。

### Trap 处理与 Syscall 分派
- strap.c：
  - 检查陷入来源（必须是用户态），读取 `scause` 判定是否为 `CAUSE_USER_ECALL`。目前 `handle_syscall()` 内仍是一个 `panic`，为 Lab1_1 的 TODO（应替换为调用 `do_syscall` 并把返回值写回 a0）。
- syscall.h / syscall.c：
  - 定义编号基于 `SYS_user_base`：目前仅 `SYS_user_print` 与 `SYS_user_exit`。
  - `do_syscall(a0..a7)`：根据 syscall 号分发到 `sys_user_print` / `sys_user_exit`。`print` 通过 `sprint()` 输出（HTIF + Spike host stdout），`exit` 调用 `shutdown()` 终止模拟机。
- 返回值路径：用户态在 `do_user_call()` 中执行 `ecall`，内核应在 `handle_syscall()` 调用 `do_syscall()` 后保留 a0，最终在恢复现场后从用户角度看到返回值。

### 用户库与应用
- app_helloworld.c：调用 `printu("Hello world!\n"); exit(0);`
- user_lib.c：
  - `printu()`：封装格式化（`vsnprintf`）+ 发起 syscall。
  - `exit(code)`：发起退出 syscall。
  - `do_user_call()`：内联汇编执行 `ecall`，将返回值存入局部变量 `ret`。
- user_lib.h：导出最小 API。

### Spike 接口 & HTIF 层
- `spike_interface/spike_utils.[ch]`：封装与 host 的交互（前端 syscalls、打印、关机、panic、断言）。`frontend_syscall()` 将参数写入共享内存数组 `magic_mem`，再通过 `htif_syscall()` 调用 Spike。
- `spike_interface/spike_file.*`：模拟文件操作（open/read/write/pread/stat/close），用于加载 ELF 和输出终端。
- `spike_interface/spike_htif.*`：HTIF 号定义、设备查询、关机实现。
- `spike_interface/spike_memory.*`：解析 DTB 获取模拟内存大小。
- `spike_interface/dts_parse.*`：解析设备树信息（为内存与外设提供元数据）。
- 原子/自旋锁（`atomic.h`）尽管当前单核，也为后续扩展（多核、并发）奠定接口。

### 通用工具层 (util)
- 基础类型与常用宏：types.h / functions.h
- 字符串与内存操作：`string.[ch]`（自实现，避免依赖 libc）
- 简易格式化输出：`snprintf.[ch]`
- 批量寄存器保存恢复宏：load_store.S
- 设计选择：全部内联或轻量实现，控制教学规模与可读性。

### 架构相关支持
- riscv.h：CSR 读写宏、异常/中断码、`riscv_regs_t` 布局，与 trapframe 紧密匹配；提供 `intr_on/off` 等简单封装。

### 配置与内存布局
- config.h：固定物理地址为用户栈、内核处理栈、trapframe 起始地址。Bare 模式下无需页表；后续 Lab2 将进入分页与更复杂内存管理。

## 已实现的“操作系统”功能层级

| 类别 | 当前具备的能力 | 说明 |
|------|----------------|------|
| 引导与特权模式 | M → S → U 三态切换 | 使用 `mret` / `sret`，完成最基本的启动链路控制 |
| 进程抽象 | 单进程（仅一个用户程序） | 无 PCB 列表、无调度，`current` 指向唯一进程 |
| Trap/系统调用 | 用户态 `ecall` → 保存现场 → 分派 → 恢复 | 仅支持 syscall，尚无普通异常/中断处理分支 |
| ELF 装载 | 解析并加载程序段、设置入口 PC | 不做段权限校验，不做重定位 |
| 输出与终止 | 通过 HTIF 与宿主交互（打印/退出） | I/O、退出均代理到 Spike host |
| 上下文保存/恢复 | 全寄存器保存与恢复 | 使用宏保证结构与偏移匹配 |
| 内存模型 | Bare 模式（VA=PA），固定地址 | 无分页、无动态分配、无保护 |
| 错误处理 | panic/assert/shutdown | 简化机制，教学友好 |
| 构建系统 | 多目标、分层静态库 | 体现分层结构（工具库 / 接口库 / 内核 / 用户） |

## 尚未实现（后续实验将逐步引入）

- 分页与虚拟内存管理（页表、`satp` 设置、缺页异常处理）
- 多进程/调度（进程创建、切换、状态管理）
- 中断处理（定时器中断、设备中断、软中断）
- 文件系统与设备驱动抽象（当前仅利用宿主文件系统加载 ELF）
- 内核/用户态系统调用扩展（比如 fork/exec/read/write/mmap 等）
- 内存分配器（内核堆、伙伴系统、slab 等）
- 权限/保护（PMP、页表权限、隔离）
- 并发与同步（真正多核、锁语义、临界区）
- 标准库/更丰富的用户空间运行时支持

## 执行路径（生命周期 Big Picture）

1. Spike 启动：加载内核到 `0x80000000`，入口 `_mentry`。
2. `_mentry` 设置栈 → `m_start()`。
3. `m_start()`：解析 DTB → 初始化 HTIF/内存 → 委托 traps 到 S 模式 → 设置 `mepc=s_start` → `mret`。
4. `s_start()`：关闭分页（Bare），调用 `load_user_program()` 装载用户 ELF（通过 `spike_file_*`）→ 准备 trapframe（栈指针、入口 PC）→ `switch_to()`。
5. `switch_to()`：设定 `stvec` 指向 `smode_trap_vector`，准备 `kernel_sp` / `kernel_trap`，清除 SPP，写入 `sepc` → `return_to_user()` → `sret` 进入用户态入口。
6. 用户程序执行 `main()` → `printu()` → `do_user_call()` → `ecall`。
7. Trap：硬件跳转 `stvec` → `smode_trap_vector` 保存寄存器 → 跳到 `smode_trap_handler()`。
8. 内核判断 `scause` →（应调用 `do_syscall()`，当前模板是 `panic`）→ 写返回值 → `switch_to(current)` → 再次 `return_to_user()`。
9. 用户继续运行直到调用 `exit()` → syscall → 内核 `sys_user_exit()` → `shutdown()` → Spike host 结束模拟。

## 关键设计意图与教学考量

- 极度简化的 `process`：让学生先掌握“陷入/返回”与上下文保存机制，再引入调度与多进程。
- 固定内存地址：避免页表、重定位、动态分配等干扰核心概念（trap/syscall、模式切换）。
- 自实现字符串/格式化：减少外部库噪音，控制可读范围；`-nostdlib` + `-fno-builtin` 强化“最小栈”概念。
- HTIF 作为“代理”接口：体现 Proxy Kernel 思路 —— 把非教学重点（真实设备驱动、复杂 I/O）代理给宿主。
- 分层拆分：util（通用）、spike_interface（宿主交互）、kernel（核心机制）、user（应用与最薄用户库）。

## 细节与潜在注意点

- `handle_syscall()` 中的 `panic` 是 Lab1_1 必修修改点：应替换为  
  `long ret = do_syscall(tf->regs.a0, tf->regs.a1, ...); tf->regs.a0 = ret;` 并继续返回。否则后续实验会因返回值丢失而异常。
- `printu()` 缓冲区固定 256 字节，长输出会截断（教学简化）。
- `sys_user_print` 返回值恒为 0，未报告写入字节数（可后续改进）。
- `shutdown()` 调用 HTIF 退出，不做资源清理（单进程模型下合理）。
- 自旋锁在单核仍出现（`frontend_syscall`），为未来多核扩展埋点。
- Bare 模式下：没有地址隔离，用户可理论上越界访问内核地址（教学阶段不防御）。
- Trapframe 与 `riscv_regs_t` 的偏移紧密耦合，修改顺序需同步汇编宏与结构体。
- `-Werror` 强制警告为错误，有利于学生保持代码整洁。
- `delegate_traps()` 仅委托常用异常，后续可扩展（比如页故障、非法指令等处理逻辑）。

## 推荐阅读/扩展顺序

1. app_helloworld.c → user_lib.c（理解 syscall 入口）
2. strap_vector.S / process.c（掌握陷入/返回路径）
3. syscall.c / strap.c（补齐分派逻辑）
4. minit.c / kernel.c（理解多层模式切换）
5. elf.c（应用装载过程）
6. `spike_interface/*`（宿主代理机制与 Proxy Kernel 核心思想）

## 总结

当前项目实现了“最小可教学”内核：具备特权模式过渡、用户程序装载、trap/syscall 基础框架、上下文保存恢复、基本 I/O 与退出机制。它不是一个完整操作系统，而是一个可渐进扩展的教学底座。后续实验将在此基础上逐层添加内存管理、进程调度、设备与文件、更多系统调用等功能。通过严格控制代码规模与明确的模块边界，学生可以逐步掌握现代 OS 的核心思想而不被工程细节淹没。  

如果你后续希望：
- 添加分页：引入页表结构、修改 `satp`、扩展 ELF 装载映射。
- 添加多进程：扩展 `process` 结构、维护进程队列、实现调度器。
- 增加 syscall：设计统一号分配策略、参数传递与错误码返回。
- 改进安全性：引入 PMP 或页表权限隔离、校验用户指针。

我可以继续为你分解下一阶段的设计或辅助修改代码，告诉我你的下一个目标即可。