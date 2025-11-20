# 谁管谁？？
## 从内核启动到运行用户程序的管理者

| 阶段 | 事件 | 管理者 |
|------|------|--------|
| **0. 加载阶段** | 将内核 ELF 加载到物理内存 0x80000000 | **spike 模拟器** |
| **1. M 态启动** | 跳转到 `_mentry`，设置栈指针 sp | **CPU（执行汇编）** |
| | 初始化 HTIF、内存大小探测 | **你的内核代码**（`m_start`） |
| | 设置 mepc=s_start，mret 切换到 S 态 | **你的内核代码** |
| **2. S 态初始化** | 关闭分页（satp=0） | **你的内核代码**（`s_start`） |
| | 初始化物理内存管理器（空闲链表） | **你的内核代码**（`pmm_init`） |
| | 构建内核页表（直映射） | **你的内核代码**（`kern_vm_init`） |
| | 开启分页（写 satp，刷 TLB） | **你的内核代码**（`enable_paging`） |
| **3. 加载用户程序** | 打开 ELF 文件 | **spike HTIF 接口** |
| | 分配物理页（trapframe/页表/栈） | **你的 PMM**（`alloc_page`） |
| | 读取 ELF 段到内存 | **spike 文件接口** + **你的代码**（`elf_load`） |
| | 建立用户页表映射（代码/栈/trapframe） | **你的 VMM**（`user_vm_map`） |
| **4. 切换到用户态** | 设置 trapframe（epc/sp/satp） | **你的内核代码**（`switch_to`） |
| | 写 satp=用户页表，sret | **CPU（执行汇编）**（`return_to_user`） |
| **5. 用户程序运行** | 执行指令、访问内存 | **CPU + MMU 硬件**（自动翻译 VA→PA） |
| | 函数调用（压栈/传参/返回） | **CPU（执行编译器生成的指令）** |
| | 系统调用（ecall） | **触发：CPU**，**处理：你的内核**（`do_syscall`） |
| **6. 系统调用处理** | 保存寄存器到 trapframe | **CPU（执行汇编）**（`smode_trap_vector`） |
| | 切换 satp=内核页表 | **CPU（执行汇编）** |
| | 切换栈到 proc->kstack | **CPU（执行汇编）** |
| | 分发系统调用（print/malloc/free） | **你的内核代码**（`handle_syscall`） |
| | 手动翻译用户 VA→PA | **你的 VMM**（`user_va_to_pa`） |
| | 打印字符串 | **spike HTIF 接口** |
| | 分配/释放物理页 | **你的 PMM/VMM** |
| | 恢复寄存器，sret 返回用户态 | **CPU（执行汇编）** |

---

## 核心分工

| 管理者 | 职责 |
|--------|------|
| **spike 模拟器** | 提供硬件环境（CPU/内存/HTIF），加载内核，模拟 MMU |
| **CPU 硬件** | 执行指令、自动翻译地址（MMU）、处理特权级切换 |
| **你的内核代码** | 内存管理（分配/映射）、进程管理、系统调用实现 |
| **编译器** | 生成栈操作指令、寄存器分配、函数调用约定 |

## 总结
我们讨论了操作系统内核的"自举"（bootstrap）问题：**spike 模拟器根据 ELF 文件将内核代码静态加载到物理地址 0x80000000，内核代码本身（包括 PMM 管理代码）不需要动态内存管理，因为它在编译时就确定了大小和位置，并在整个系统运行期间永久占用；内核启动后，PMM 接管从 `_end` 到 `PHYS_TOP` 的剩余物理内存，通过 `alloc_page`/`free_page` 为用户进程、内核堆、页表等提供动态内存分配，而函数调用时的栈操作和寄存器管理由 CPU 执行编译器生成的指令自动完成**——简而言之，内核通过"自己管理别人，自己被静态预留"的方式实现了内存管理的冷启动。

---

## 简化理解

- **静态部分**（启动前）：spike 加载
- **动态管理**（运行时）：
  - **粗粒度**（页级）：你的代码管理（`alloc_page`/`user_vm_map`）
  - **细粒度**（字节级）：CPU+编译器自动处理（栈、寄存器）
- **硬件翻译**：MMU 根据 satp 指向的页表自动完成 VA→PA
# 内核启动过程
`_mentry` -> `m_start` -> `s_start` -> `load_user_program`
## 来自Gemini的更具体的回答
### PKE代理内核启动及应用加载流程

这个流程描述了从执行`spike`命令开始，到用户应用程序的第一条指令被执行为止的完整过程。

1.  **Spike模拟器启动 (Host)**
    *   用户执行命令: `$ spike ./obj/riscv-pke ./obj/app_xxx`。
    *   `spike`模拟器创建一个RISC-V硬件环境（hart），并将物理内存映射到`0x80000000`开始的地址。
    *   `spike`将`riscv-pke`这个ELF可执行文件加载到模拟的物理内存中。由于`riscv-pke`的链接脚本(`kernel.lds`)将其起始地址也设置为`0x80000000`，因此虚拟地址与物理地址在内核启动阶段是直接对应的。
    *   `spike`将PC（程序计数器）设置为`riscv-pke`的入口点地址，开始执行。

2.  **`_mentry` (in `kernel/machine/mentry.S`) - M模式入口**
    *   这是PKE内核的第一条指令，运行在**机器模式(M-mode)**。
    *   为当前hart（CPU核心）设置一个大小为4KB的内核栈。
    *   跳转到C函数`m_start`。

3.  **`m_start` (in `kernel/machine/minit.c`) - M模式初始化**
    *   初始化HTIF（主机-目标机接口），使得内核可以通过`sprint`等函数与主机交互（如打印信息）。
    *   **设置特权级转换**：将`mstatus`寄存器的MPP字段设置为S模式，这意味着下一次执行`mret`指令时，特权级将从M模式**切换到S模式(Supervisor-mode)**。
    *   **设置S模式入口**：将S模式的入口函数`s_start`的地址写入`mepc`（Machine Exception Program Counter）寄存器。`mret`指令会跳转到`mepc`指定的地址。
    *   **委托中断和异常**：调用`delegate_traps()`，将大部分中断和异常（如系统调用`ecall`）的处理权**委托**给S模式，这样内核的主要逻辑就可以在S模式下运行。
    *   执行`mret`指令，CPU从M模式切换到S模式，并跳转到`s_start`函数开始执行。

4.  **`s_start` (in `kernel/kernel.c`) - S模式初始化**
    *   此时CPU已运行在**S模式**。
    *   设置`satp`寄存器为0，禁用分页机制，采用**直接地址映射**（虚拟地址=物理地址）。这在lab1中是关键设定。
    *   调用`load_user_program()`来加载用户应用程序。
    *   调用`switch_to()`来准备并切换到用户应用程序执行。

5.  **`load_user_program` & `load_bincode_from_host_elf` (in `kernel/kernel.c`, `kernel/elf.c`) - 加载应用**
    *   为用户进程分配并初始化`trapframe`（用于保存上下文）和内核栈。
    *   通过HTIF接口(`spike_file_open`)打开命令行中指定的应用程序ELF文件（如`app_xxx`）。
    *   解析ELF文件的头部和程序段（segment）信息。
    *   将ELF文件中的`.text`（代码）和`.data`（数据）等段加载到指定的物理内存地址中（根据`user.lds`链接脚本指定，如`0x81000000`）。
    *   将ELF文件的入口点地址保存到进程的`trapframe->epc`中。

6.  **`switch_to` (in `kernel/process.c`) - 切换到用户态**
    *   设置S模式的**陷阱处理入口**`stvec`寄存器，使其指向`smode_trap_vector`。这样，当用户程序发生系统调用、异常或中断时，CPU会跳转到这里。
    *   **设置特权级转换**：修改`sstatus`寄存器，将下一次`sret`后的目标特权级设置为**U模式(User-mode)**，并开启用户态中断。
    *   **设置U模式入口**：将之前保存在`trapframe->epc`中的用户程序入口地址写入`sepc`（Supervisor Exception Program Counter）寄存器。
    *   调用汇编函数`return_to_user`。

7.  **`return_to_user` (in `kernel/strap_vector.S`) - 进入用户态**
    *   将`trapframe`的地址保存到`sscratch`寄存器中，备用。
    *   **恢复上下文**：从`trapframe`中恢复所有通用寄存器。
    *   执行`sret`指令。CPU从S模式切换到U模式，PC跳转到`sepc`指定的地址。
    *   **用户应用程序开始执行。**


### PKE系统调用处理流程

这个流程描述了用户程序执行`ecall`指令，内核完成服务并返回用户程序的完整过程。

1.  **应用程序发起调用 (U-mode)**
    *   应用程序调用库函数（如`printu`），该函数最终调用`do_user_call`。
    *   在`do_user_call`中，编译器将系统调用号放入`a0`寄存器，其他参数放入`a1-a7`寄存器。
    *   执行`ecall`指令。

2.  **陷入内核 (Trap)**
    *   `ecall`指令触发一个“来自U模式的环境调用”异常，CPU特权级从**U模式自动切换到S模式**。
    *   CPU根据`stvec`寄存器的值，跳转到预设的陷阱处理入口`smode_trap_vector`。

3.  **`smode_trap_vector` (in `kernel/strap_vector.S`) - S模式陷阱入口**
    *   **保存上下文**：
        *   使用`csrrw`指令，将`a0`寄存器与`sscratch`寄存器（该寄存器在`switch_to`时已保存了`trapframe`的地址）交换。这样，`a0`现在指向当前进程的`trapframe`。
        *   调用`store_all_registers`宏，将所有通用寄存器的值**保存**到`trapframe`中。
    *   **切换栈**：将`sp`（栈指针）切换到当前进程的**内核栈**（`proc->kstack`），而不是使用PKE内核自身的全局栈。
    *   跳转到C语言编写的陷阱处理函数`smode_trap_handler`。

4.  **`smode_trap_handler` (in `strap.c`) - S模式陷阱分发**
    *   将`sepc`寄存器（保存了`ecall`指令的地址）的值存入`current->trapframe->epc`。
    *   读取`scause`寄存器，判断陷阱原因。发现是`CAUSE_USER_ECALL`（系统调用）。
    *   调用`handle_syscall()`处理系统调用。
    *   处理完毕后，调用`switch_to(current)`返回用户进程。

5.  **`handle_syscall` (in `strap.c`) - 系统调用处理**
    *   **`tf->epc += 4`**：将保存在`trapframe`中的返回地址加4。这是因为`ecall`本身是一条指令，返回时必须跳到下一条指令继续执行，否则会陷入无限`ecall`循环。
    *   从`trapframe`中取出`a0-a7`寄存器的值作为参数。
    *   调用`do_syscall()`，并将这些参数传递过去。
    *   将`do_syscall()`的返回值写回`trapframe`的`a0`寄存器域（`tf->regs.a0`），这是用户程序获取返回值的标准方式。

6.  **`do_syscall` (in `syscall.c`) - 系统调用功能实现**
    *   根据传入的系统调用号（来自`a0`），使用`switch`语句分发到具体的内核功能函数（如`sys_user_print`）。
    *   执行相应的功能（如通过HTIF打印字符串）。
    *   返回处理结果（成功或失败码）。

7.  **返回用户态 (Return from Trap)**
    *   `handle_syscall`返回到`smode_trap_handler`。
    *   `smode_trap_handler`调用`switch_to()`。
    *   `switch_to()`最终调用`return_to_user`。
    *   `return_to_user`从`trapframe`中**恢复所有寄存器**（包括被修改的`epc`和存放返回值的`a0`）。
    *   执行`sret`指令，CPU从S模式切换回U模式，并跳转到`epc`（即`ecall`的下一条指令）继续执行。
    *   应用程序从`ecall`的下一条指令继续执行，并在`a0`寄存器中获得系统调用的返回值。


# ecall系统调用处理流程

## 1. 用户空间触发
- 用户程序调用`do_user_call`函数
- 参数按RISC-V ABI自动加载到寄存器a0-a7
- 执行`ecall`指令触发系统调用

## 2. 硬件处理
- 处理器从UMODE切换到SMODE
- 硬件跳转到stvec寄存器指定的trap向量

## 3. 上下文保存
- 保存所有通用寄存器到trapframe
- 切换到内核栈
- 跳转到C语言trap处理函数

## 4. 内核处理
- `smode_trap_handler`确认trap来源
- 识别为用户ecall后调用`handle_syscall`
- `handle_syscall`调用`do_syscall`处理具体系统调用

## 5. 系统调用分发
- 根据a0中的系统调用号分发到具体函数
- 执行对应功能（如打印、退出等）

## 6. 返回用户空间
- 通过switch_to返回用户态
- 恢复寄存器上下文
- 从sepc继续执行用户程序

# trapframe详解

trapframe是一个数据结构，用于在发生trap时保存用户进程的上下文（寄存器状态）。

## 结构定义

```c
typedef struct trapframe_t {
  riscv_regs regs;      // 通用寄存器状态（偏移0）
  uint64 kernel_sp;     // 内核栈指针（偏移248）
  uint64 kernel_trap;   // trap处理函数地址（偏移256）
  uint64 epc;           // 用户程序计数器（偏移264）
} trapframe;
```

## 成员说明

1. `regs`：保存所有通用寄存器的值，包括ra, sp, gp, tp, t0-t6, a0-a7, s0-s11等
2. `kernel_sp`：指向进程的内核栈，用于trap处理时的栈空间
3. `kernel_trap`：指向smode_trap_handler函数，用于trap处理时的函数跳转
4. `epc`：保存用户程序的程序计数器（Exception Program Counter），用于返回用户态时恢复执行

## 内存布局

在PKE中，trapframe被固定放置在物理地址`0x81300000`（USER_TRAP_FRAME），这是在Bare模式下使用的固定地址方案。

## trapframe初始化过程

trapframe在系统启动时由内核初始化：

1. 在`load_user_program`函数中初始化：
   - 将trapframe指针设置为固定地址`0x81300000`（USER_TRAP_FRAME）
   - 清空整个trapframe结构体内存
   - 设置进程的内核栈地址`kstack`为`0x81200000`（USER_KSTACK）
   - 设置用户栈指针`regs.sp`为`0x81100000`（USER_STACK）

2. 在`switch_to`函数中设置trapframe的特定字段：
   - `kernel_sp`设置为进程的内核栈地址（proc->kstack）
   - `kernel_trap`设置为smode_trap_handler函数地址
   - `epc`设置为用户程序入口点

3. 在`return_to_user`函数中，将trapframe地址保存到sscratch寄存器，供trap处理时使用

## trapframe的归属

trapframe属于内核管理的数据结构，但用于保存特定用户进程的上下文信息：

1. **物理归属**：trapframe在内存中的存储空间由内核分配和管理，位于内核地址空间中

2. **逻辑归属**：每个trapframe逻辑上与一个用户进程相关联，用于保存该进程的上下文状态

3. **生命周期**：trapframe的生命周期由内核管理，在进程创建时分配，在进程终止时释放（在PKE中由于只有一个进程，所以trapframe在整个系统运行期间都存在）

4. **访问权限**：trapframe只能在内核态访问，用户进程无法直接访问自己的trapframe

5. **作用域**：虽然trapframe保存的是用户进程的寄存器状态，但它本身是内核数据结构，用于内核在处理trap时保存和恢复进程上下文

在PKE中，trapframe被固定放置在物理地址`0x81300000`，这是内核为用户进程预留的固定内存区域。虽然从逻辑上讲，trapframe保存的是用户进程的上下文信息，但物理上它属于内核管理的内存空间。

## 用户寄存器保存到trapframe的实现

将用户寄存器值保存到trapframe中的逻辑在汇编代码中实现：

1. **入口点**：在`smode_trap_vector`函数中开始执行寄存器保存

2. **地址获取**：
   ```assembly
   csrrw a0, sscratch, a0    # 交换a0和sscratch，使a0指向trapframe
   addi t6, a0, 0           # 将trapframe地址复制到t6
   ```

3. **批量保存**：
   ```assembly
   store_all_registers      # 调用宏保存所有寄存器
   ```

4. **特殊处理a0寄存器**：
   ```assembly
   csrr t0, sscratch        # 从sscratch恢复原始的a0值
   sd t0, 72(a0)           # 将原始a0值保存到trapframe中正确位置
   ```

`store_all_registers`宏在`util/load_store.S`中定义，逐个将寄存器保存到trapframe的相应偏移位置：

```assembly
.macro store_all_registers
    sd ra, 0(t6)      # 保存ra寄存器到偏移0
    sd sp, 8(t6)      # 保存sp寄存器到偏移8
    # ... 其他寄存器保存
    sd a0, 72(t6)     # 保存a0寄存器到偏移72（注意：这个a0是交换后的值）
    # ... 其他寄存器保存
.endm
```

需要注意的是，由于前面执行了`csrrw a0, sscratch, a0`指令，此时的a0寄存器值已经不是用户程序原来的值，而是指向trapframe的指针。因此需要在执行`store_all_registers`之后，从sscratch寄存器中恢复用户程序原来的a0值，并将其保存到trapframe中的正确位置。

## 系统架构位数

该项目实现的是64位操作系统：

1. **工具链前缀**：使用`riscv64-unknown-elf-`前缀的工具链，表明是64位RISC-V架构

2. **数据类型**：代码中广泛使用`uint64`类型，如trapframe结构体中的字段都是64位

3. **寄存器宽度**：所有寄存器相关的操作都是64位的，如`uint64 ra`, `uint64 sp`等

4. **指令集**：代码注释中提到"RV64G"，这是RISC-V 64位通用指令集架构

5. **ABI**：Makefile中使用`lp64`作为ABI（应用程序二进制接口），这是64位RISC-V的ABI

6. **编译选项**：虽然Makefile中没有明确指定`-march=`参数，但使用了`-mcmodel=medany`，这通常用于64位系统

因此，该项目是为64位RISC-V架构实现的操作系统。

## 调试方法

该项目支持多种调试方法：

### 1. 基本运行
使用以下命令编译并运行项目：
```bash
$ make run
```

这会启动Spike模拟器运行内核和用户程序。

### 2. GDB调试
项目支持使用GDB进行源码级调试：

1. **启动调试会话**：
   ```bash
   $ make gdb
   ```
   
   这个命令会：
   - 启动Spike模拟器并监听9824端口
   - 启动OpenOCD作为调试代理
   - 启动GDB并加载调试配置

2. **调试配置**：
   - `.spike.cfg`文件配置了OpenOCD连接到Spike的远程位bang接口
   - GDB通过OpenOCD连接到Spike模拟器进行调试

3. **调试功能**：
   - 可以设置断点、单步执行、查看变量等
   - 支持内核和用户程序的调试
   - 可以检查寄存器状态、内存内容等

### 3. 清理调试环境
如果需要清理调试环境，可以使用：
```bash
$ make gdb_clean
```

这会终止所有相关的调试进程。

### 4. 其他调试技巧
- 可以在代码中添加`sprint`语句进行日志输出
- 使用`objdump`目标查看反汇编代码：
  ```bash
  $ make objdump
  ```
- 可以直接使用`spike`命令行工具运行程序并查看输出

----

# lab1_2 异常处理
好的，我们来详细解析 **lab1_2 异常处理** 的思路、实现步骤以及涉及的核心知识点。

### 实验目标解析

lab1_2的核心目标是让我们的PKE操作系统内核能够正确处理一个来自用户程序的**异常（Exception）**。

具体来说，给定应用 `app_illegal_instruction.c` 会尝试在权限较低的用户模式（U-mode）执行一条只有在更高权限模式下才能执行的特权指令 (`csrw sscratch, 0`)。这种行为是非法的，CPU硬件会检测到这个错误，并触发一个“非法指令异常”。

我们的任务就是修改PKE内核，捕获这个异常，打印出明确的错误信息（"Illegal instruction!"），然后安全地终止系统，而不是像初始代码那样直接 `panic` 崩溃。

### 核心知识点

要完成这个实验，需要理解以下几个关键概念：

1.  **RISC-V特权级 (Privilege Levels)**：
    *   **U-mode (User)**：用户模式，权限最低，用于运行应用程序。不能执行访问物理内存、修改特权寄存器等敏感操作。
    *   **S-mode (Supervisor)**：监督模式，权限较高，用于运行操作系统内核的大部分功能。
    *   **M-mode (Machine)**：机器模式，权限最高，用于最底层的硬件初始化和管理，如多核启动、中断/异常的最终分发等。

2.  **异常 (Exception)**：由当前正在执行的指令同步（synchronously）引发的非正常控制流转移。例如：非法指令、访问不存在的内存地址、除零等。这与**中断（Interrupt）**不同，中断是异步（asynchronously）的，由外部事件（如定时器、I/O设备）引发。

3.  **陷阱处理 (Trap Handling)**：当异常或中断发生时，CPU会暂停当前执行流，根据预设的配置，跳转到一个特定的地址去执行一段被称为“陷阱处理程序”（Trap Handler）的代码。这个过程通常伴随着特权级的提升（例如从U-mode -> S-mode或M-mode）。

4.  **陷阱委托 (Trap Delegation)**：RISC-V的一个重要机制。默认情况下，所有陷阱（异常和中断）都由最高权限的M-mode处理。但是，M-mode可以通过设置 `medeleg` (Machine Exception Delegation) 和 `mideleg` (Machine Interrupt Delegation) 寄存器，将特定类型的陷阱“委托”给S-mode处理。**如果一个陷阱没有被委托，那么它必须在M-mode中处理。**

### 详细思路与代码追踪

让我们跟着代码的执行流，一步步分析问题所在并找到解决方案。

#### 第一步：分析异常的源头

在 `user/app_illegal_instruction.c` 中，第13行代码是关键：
```c
asm volatile("csrw sscratch, 0");
```
*   `csrw` 是一条写控制状态寄存器（CSR）的指令。
*   `sscratch` 是一个S-mode的暂存寄存器。
*   应用程序运行在U-mode，尝试写入一个S-mode的寄存器，这违反了RISC-V的特权级保护机制。
*   因此，当CPU执行到这条指令时，会立即触发一个“非法指令异常” (`Illegal Instruction Exception`)。

#### 第二步：确定陷阱由哪个模式处理

当异常发生时，CPU需要决定跳转到S-mode的陷阱处理程序还是M-mode的。这取决于 `medeleg` 寄存器是否设置了委托该异常。

我们查看内核初始化代码 `kernel/machine/minit.c` 中的 `delegate_traps()` 函数：
```c
// kernel/machine/minit.c
static void delegate_traps() {
  // ...
  uintptr_t exceptions = (1U << CAUSE_MISALIGNED_FETCH) | (1U << CAUSE_FETCH_PAGE_FAULT) |
                         (1U << CAUSE_BREAKPOINT) | (1U << CAUSE_LOAD_PAGE_FAULT) |
                         (1U << CAUSE_STORE_PAGE_FAULT) | (1U << CAUSE_USER_ECALL);

  write_csr(medeleg, exceptions);
  // ...
}
```
`exceptions` 这个位掩码（bitmask）定义了所有要从M-mode委托给S-mode的异常类型。我们查看 `kernel/riscv.h` 可以找到 `CAUSE_ILLEGAL_INSTRUCTION` 对应的值是 `2`。

很明显，`(1U << CAUSE_ILLEGAL_INSTRUCTION)` **并不在** `exceptions` 这个位掩码中。这意味着**非法指令异常没有被委托给S-mode**。

**结论：** 这个异常必须在最高权限的 **M-mode** 中处理。

#### 第三步：追踪M-mode的陷阱处理流程

1.  **陷阱入口 (mtvec)**：CPU在M-mode遇到陷阱时，会跳转到 `mtvec` 寄存器指向的地址。在 `kernel/machine/minit.c` 的 `m_start` 函数中，内核已经设置好了这个入口：
    ```c
    // kernel/machine/minit.c
    write_csr(mtvec, (uint64)mtrapvec);
    ```
    它指向了 `kernel/machine/mtrap_vector.S` 中定义的 `mtrapvec`。

2.  **汇编处理程序 (`mtrapvec`)**：这个汇编函数负责：
    *   保存所有通用寄存器（上下文），防止被C函数破坏。
    *   设置M-mode自己使用的栈。
    *   调用C语言编写的核心处理函数 `handle_mtrap`。
    *   `handle_mtrap` 返回后，恢复所有寄存器。
    *   执行 `mret` 指令返回到异常发生前的地方。

3.  **C语言处理程序 (`handle_mtrap`)**：这是我们需要修改的地方。代码位于 `kernel/machine/mtrap.c`：
    ```c
    // kernel/machine/mtrap.c
    void handle_mtrap() {
      uint64 mcause = read_csr(mcause);
      switch (mcause) {
        // ... 其他 case ...
        case CAUSE_ILLEGAL_INSTRUCTION:
          // TODO (lab1_2): call handle_illegal_instruction to implement ...
          panic( "call handle_illegal_instruction to accomplish illegal instruction interception for lab1_2.\n" );
          break;
        // ... 其他 case ...
      }
    }
    ```
    *   函数首先读取 `mcause` 寄存器，这个寄存器记录了陷阱发生的原因。
    *   通过 `switch` 语句，代码找到了匹配 `CAUSE_ILLEGAL_INSTRUCTION` 的分支。
    *   当前实现是直接调用 `panic`，这会导致程序立即停止并打印我们看到的提示信息。这就是实验运行结果的来源。

#### 第四步：实现解决方案

实验指导和 `TODO` 注释已经给出了明确的指示：将 `panic` 调用替换为对 `handle_illegal_instruction()` 函数的调用。

我们先找到 `handle_illegal_instruction()` 函数的定义，它也在 `kernel/machine/mtrap.c` 文件中：
```c
// kernel/machine/mtrap.c
static void handle_illegal_instruction() {
  sprint("Illegal instruction!\n");
  do_exit(-1);
}
```
这个函数做了两件事：
1.  `sprint("Illegal instruction!\n")`: 打印出我们期望看到的错误信息。
2.  `do_exit(-1)`: 调用系统退出函数，并传入-1作为退出码，安全地关闭系统。

现在，我们只需要进行修改即可。

**修改文件**: `kernel/machine/mtrap.c`

**修改前**:
```c
    case CAUSE_ILLEGAL_INSTRUCTION:
      // TODO (lab1_2): call handle_illegal_instruction to implement illegal instruction
      // interception, and finish lab1_2.
      panic( "call handle_illegal_instruction to accomplish illegal instruction interception for lab1_2.\n" );

      break;
```

**修改后**:
```c
    case CAUSE_ILLEGAL_INSTRUCTION:
      // TODO (lab1_2): call handle_illegal_instruction to implement illegal instruction
      // interception, and finish lab1_2.
      // panic( "call handle_illegal_instruction to accomplish illegal instruction interception for lab1_2.\n" );
      handle_illegal_instruction(); // 将 panic 替换为函数调用
      break;
```

### 实验步骤与验证

1.  **切换分支并合并**：
    ```bash
    # 确保 lab1_1 的工作已提交
    $ git commit -a -m "my work on lab1_1 is done."
    
    # 切换到 lab1_2 分支
    $ git checkout lab1_2_exception
    
    # 合并 lab1_1 的修改
    $ git merge lab1_1_syscall -m "continue to work on lab1_2"
    ```

2.  **修改代码**：
    打开 `kernel/machine/mtrap.c` 文件，找到 `handle_mtrap` 函数中的 `switch` 语句，将 `case CAUSE_ILLEGAL_INSTRUCTION:` 分支下的 `panic(...)` 调用替换为 `handle_illegal_instruction();`。

3.  **编译和运行**：
    ```bash
    $ make clean; make
    $ spike ./obj/riscv-pke ./obj/app_illegal_instruction
    ```

4.  **验证结果**：
    运行后，你将看到如下输出，这与实验要求的预期结果完全一致：
    ```bash
    In m_start, hartid:0
    HTIF is available!
    (Emulated) memory size: 2048 MB
    Enter supervisor mode...
    Application: ./obj/app_illegal_instruction
    Application program entry point (virtual address): 0x0000000081000000
    Switching to user mode...
    Going to hack the system by running privilege instructions.
    Illegal instruction!
    System is shutting down with exit code -1.
    ```
    可以看到，内核成功捕获了非法指令异常，打印了正确的提示信息，然后正常关闭，实验完成。

5.  **提交工作**：
    ```bash
    $ git commit -a -m "my work on lab1_2 is done."
    ```

### 总结

lab1_2 是一个理解异常处理和特权级机制的绝佳练习。通过这个实验，我们深入了解了：
-   用户程序如何因为执行非法操作而触发硬件异常。
-   RISC-V的陷阱委托机制如何决定异常是在S-mode还是M-mode处理。
-   内核中M-mode陷阱处理程序的完整工作流程：从汇编入口到C语言分发。
-   如何通过修改内核代码，将一个致命的 `panic` 替换为一个优雅的、信息明确的异常处理流程。

### g_itrgrame
#### g_itrframe是什么

g_itrframe是一个全局的中断帧（interrupt frame）结构体，定义在[minit.c](file:///app/riscv-pke/kernel/machine/minit.c)中：

```c
riscv_regs g_itrframe;
```

它是一个[riscv_regs](file:///app/riscv-pke/kernel/riscv.h#L137-L169)类型的全局变量，用于在M模式（机器模式）下发生中断或异常时保存处理器寄存器状态。在[mtrap_vector.S](file:///app/riscv-pke/kernel/machine/mtrap_vector.S)中，当M模式陷阱发生时，会将所有寄存器保存到这个结构体中。

#### trapframe是什么

trapframe是用户进程的陷阱帧结构体，定义在[process.h](file:///app/riscv-pke/kernel/process.h)中：

```c
typedef struct trapframe_t {
  // space to store context (all common registers)
  riscv_regs regs;

  // process's "user kernel" stack
  uint64 kernel_sp;
  // pointer to smode_trap_handler
  uint64 kernel_trap;
  // saved user process counter
  uint64 epc;
} trapframe;
```

每个用户进程都有一个与之关联的trapframe，用于在用户态发生陷阱（如系统调用）时保存进程的上下文状态。

#### 两者之间的关系

1. **作用域不同**：
   - g_itrframe是全局的，用于M模式（机器模式）下的中断处理
   - trapframe是每个进程一个，用于S模式（监督模式）下的陷阱处理

2. **使用场景不同**：
   - g_itrframe用于处理机器级别的中断，如硬件中断
   - trapframe用于处理用户进程的系统调用、页面错误等

3. **结构差异**：
   - g_itrframe只是[riscv_regs](file:///app/riscv-pke/kernel/riscv.h#L137-L169)结构体，保存寄存器状态
   - trapframe除了包含寄存器状态外，还包含内核栈指针、陷阱处理函数指针和程序计数器等额外信息

4. **初始化方式不同**：
   - g_itrframe的地址通过[write_csr(mscratch, &g_itrframe)](file:///app/riscv-pke/kernel/riscv.h#L91-L92)设置到mscratch寄存器中
   - trapframe的地址通过[return_to_user](file:///app/riscv-pke/kernel/strap_vector.S#L44-L55)函数设置到sscratch寄存器中

总的来说，g_itrframe和trapframe都是用于保存处理器状态的结构体，但它们服务于不同级别的异常处理机制。g_itrframe处理底层的机器模式中断，而trapframe处理用户进程的监督模式陷阱。

---

# 1ab1_3 
####  实验指导

本实验的核心是理解和实现一个完整的中断处理流程。在RISC-V中，中断和异常（统称为trap）的处理涉及多个特权级。时钟中断（timer interrupt）是一种由硬件定时器触发的外部中断，它允许操作系统周期性地夺回CPU控制权，即使用户程序正在运行。这对于实现分时多任务系统至关重要。

让我们来跟踪一下PKE中时钟中断的处理流程：

**1. 中断的初始化**

在内核启动时，`m_start` 函数 (位于 `kernel/machine/minit.c`) 调用了 `timerinit()` 函数。
```c
 // kernel/machine/minit.c
 82 void timerinit(uintptr_t hartid) {
 83   // fire timer irq after TIMER_INTERVAL from now.
 84   *(uint64*)CLINT_MTIMECMP(hartid) = *(uint64*)CLINT_MTIME + TIMER_INTERVAL;
 85
 86   // enable machine-mode timer irq in MIE (Machine Interrupt Enable) csr.
 87   write_csr(mie, read_csr(mie) | MIE_MTIE);
 88 }
```
这个函数做了两件事：
- **第84行**: 设置下一次中断的触发时间。它读取当前时间 (`CLINT_MTIME`)，加上一个固定的时间间隔 (`TIMER_INTERVAL`)，然后将结果写入时间比较寄存器 (`CLINT_MTIMECMP`)。当 `CLINT_MTIME` 的值增长到等于 `CLINT_MTIMECMP` 的值时，硬件就会触发一个时钟中断。
- **第87行**: 打开机器模式（M-mode）的时钟中断使能位 (`MIE_MTIE`)。这告诉CPU，我们允许处理来自定时器的中断请求。

**2. 中断在 M-Mode 的捕获和委托**

当时钟中断发生时，由于这是 M-Mode 级别的中断，CPU会陷入到 M-Mode，并跳转到 `mtvec` 寄存器指向的地址，即 `mtrapvec` (`kernel/machine/mtrap_vector.S`)。这个汇编例程会保存上下文，然后调用C函数 `handle_mtrap` (`kernel/machine/mtrap.c`)。

```c
 // kernel/machine/mtrap.c
 30 void handle_mtrap() {
 31   uint64 mcause = read_csr(mcause);
 32   switch (mcause) {
 33     case CAUSE_MTIMER:
 34       handle_timer();
 35       break;
       ... // 其他异常处理
 36   }
 37 }
```
`handle_mtrap` 读取 `mcause` 寄存器来判断中断原因。当原因是时钟中断 (`CAUSE_MTIMER`) 时，它调用 `handle_timer`。

```c
 // kernel/machine/mtrap.c
 18 static void handle_timer() {
 19   int cpuid = 0;
 20   // setup the timer fired at next time (TIMER_INTERVAL from now)
 21   *(uint64*)CLINT_MTIMECMP(cpuid) = *(uint64*)CLINT_MTIMECMP(cpuid) + TIMER_INTERVAL;
 22
 23   // setup a soft interrupt in sip (S-mode Interrupt Pending) to be handled in S-mode
 24   write_csr(sip, SIP_SSIP);
 25 }
```
`handle_timer` 函数非常关键，它扮演了一个“二传手”的角色：
- **第21行**: 立即重新设置下一次时钟中断的时间。**这是必须的**，否则中断将只会发生一次。
- **第24行**: 它并不在M-Mode下完成所有处理，而是通过向 `sip` (Supervisor Interrupt Pending) 寄存器写入 `SIP_SSIP`，来触发一个发往S-Mode的“软件中断”。

**思考：** 为什么不直接在M-Mode处理完所有逻辑？这是RISC-V特权级分离设计思想的体现。M-Mode 应该只负责最底层的硬件管理，保持其代码尽可能小而可靠。操作系统的核心逻辑，如进程调度（时钟中断的主要目的），应该在S-Mode中实现。因此，M-Mode捕获硬件中断，然后将其“委托”给S-Mode处理，是一种优雅的实现方式。

**3. 中断在 S-Mode 的处理**

当M-Mode的 `mret` 指令返回后，CPU检测到 `sip` 寄存器中有待处理的S-Mode中断，于是会**立即再次陷入**，但这次是进入S-Mode。S-Mode的 trap 入口是 `smode_trap_vector`，最终调用 `smode_trap_handler` (`kernel/strap.c`)。

```c
 // kernel/strap.c
 48 void smode_trap_handler(void) {
     ...
 59   uint64 cause = read_csr(scause);
 60
 61   if (cause == CAUSE_USER_ECALL) {
 62     handle_syscall(current->trapframe);
 63   } else if (cause == CAUSE_MTIMER_S_TRAP) {  // soft trap generated by timer interrupt in M mode
 64     handle_mtimer_trap();
 65   } else {
     ... // 其他异常处理
 66   }
     ...
 73   switch_to(current);
 74 }
```
在这里，`scause` 的值是 `CAUSE_MTIMER_S_TRAP`（S-Mode 软件中断），于是 `handle_mtimer_trap` 被调用。

**4. 找到并修复问题**

现在我们来看 `handle_mtimer_trap` 函数，问题的根源就在这里：

```c
 // kernel/strap.c
 31 static uint64 g_ticks = 0;
 ...
 35 void handle_mtimer_trap() {
 36   sprint("Ticks %d\n", g_ticks);
 37   // TODO (lab1_3): increase g_ticks to record this "tick", and then clear the "SIP"
 38   // field in sip register.
 39   // hint: use write_csr to disable the SIP_SSIP bit in sip.
 40   panic( "lab1_3: increase g_ticks by one, and clear SIP field in sip register.\n" );
 41 }
```
代码执行到这里，会打印出 `Ticks 0`，然后调用 `panic`，导致系统崩溃。这与我们观察到的现象完全一致。

根据 `TODO` 的提示，我们需要完成两件事：
1.  **增加 `g_ticks` 的值**: 这个全局变量用来记录时钟中断发生的次数。我们只需将其加一即可。
2.  **清除 `sip` 寄存器中的 `SIP_SSIP` 位**: M-Mode 设置了这个位来通知 S-Mode。现在 S-Mode 已经收到了通知并开始处理，就必须将这个“通知标志”清除掉。**如果不清除，当中断处理返回后，CPU会认为还有一个待处理的中断，从而再次陷入，导致无限的中断循环，系统卡死。**

**最终实现**

我们将 `panic` 调用替换为以下两行代码：
- `g_ticks++;`
- `write_csr(sip, read_csr(sip) & ~SIP_SSIP);`
    - `read_csr(sip)`: 读取 `sip` 寄存器的当前值。
    - `~SIP_SSIP`: `SIP_SSIP` 是一个掩码（值为 `0x2`，即 `0b10`）。`~` 是按位取反操作，所以 `~SIP_SSIP` 会得到一个除了第二位是0、其他位都是1的掩码。
    - `&`: 按位与操作。用当前值和这个掩码做“与”运算，效果就是精确地将 `SIP_SSIP` 对应的位清零，同时保持其他位不变。这是清除特定标志位的标准做法。

**思考解答：** 为什么死循环程序不会导致死机？
因为时钟中断是**硬件强制**的。无论CPU正在执行什么指令（即使是 `j .` 这样的死循环指令），只要定时器到达预设时间，硬件就会打断当前的执行流，强制CPU去执行中断服务例程（即我们的trap处理代码）。在中断服务例程中，操作系统夺回了控制权，它可以执行自己的任务（比如更新时钟、调度其他进程等），然后再决定是否以及何时返回到原来的死循环程序。这种由外部硬件事件强制打断程序执行的机制，正是**抢占式（preemptive）系统**的基础。

**实验完毕后，记得提交修改（命令行中-m后的字符串可自行确定），以便在后续实验中继承lab1_3中所做的工作**：

```bash
$ git commit -a -m "my work on lab1_3 is done."
```

#### **参考答案**

修改 `kernel/strap.c` 文件中的 `handle_mtimer_trap` 函数，如下所示：

```c
// kernel/strap.c

static uint64 g_ticks = 0;
//
// added @lab1_3
//
void handle_mtimer_trap() {
  sprint("Ticks %d\n", g_ticks);
  // TODO (lab1_3): increase g_ticks to record this "tick", and then clear the "SIP"
  // field in sip register.
  // hint: use write_csr to disable the SIP_SSIP bit in sip.
  // panic( "lab1_3: increase g_ticks by one, and clear SIP field in sip register.\n" );

  // record the tick
  g_ticks++;

  // clear the software interrupt pending bit in sip
  write_csr(sip, read_csr(sip) & ~SIP_SSIP);
}
```

# lab2

### 内存布局
Summarized conversation history好的。先给出一张对照表，按区域汇总 lab1（Bare 直映射）与 lab2_1（SV39 分页）下的内存布局与权限差异，尽量贴合附件描述。

| 区域/对象 | lab1 映射与地址 | lab2_1 映射与地址 | 权限/可见性 | 说明 |
|---|---|---|---|---|
| 物理内存范围 | PA: [0x80000000, 0x80000000+size) | 同左 | — | spike 模拟内存从 0x80000000 起 |
| 内核镜像 .text/.rodata | VA=PA，从 0x80000000 起 | VA≈PA（内核页表直映射） | S，.text RX，.rodata R | 由 `kernel.lds` 固定起址 |
| 内核 .data/.bss | VA=PA，紧随内核代码段 | VA≈PA（直映射） | S，RW | |
| HTIF 内存窗口 | VA=PA，位于内核直映射区域 | VA≈PA（直映射） | S，RW | 由 DTS 发现并用于主机交互（打印/文件） |
| S 态陷入向量 `stvec` | 指向内核直映射中的向量页 | 同左（在内核页表中映射） | S，RX | `smode_trap_vector` |
| M 态陷入向量 `mtvec` | 指向内核直映射中的向量页 | 同左 | M，RX | `mtrapvec` |
| 每进程内核栈 kstack | VA=PA，内核直映射页 | VA≈PA（直映射） | S，RW | U→S 陷入后切到该栈 |
| 每进程 trapframe | VA=PA，内核直映射页/结构体 | VA≈PA（直映射） | S，RW | 保存通用寄存器、`kernel_sp/kernel_satp/epc` |
| 内核页表根 | 未使用分页 | `satp(kernel)` 指向直映射页表 | S | 仅内核使用，VA≈PA |
| 用户代码/数据段 | VA=PA（由 ELF vaddr，例：0x81000000） | VA=ELF vaddr → PA=分配页，经 user 页表映射 | U，段权限 R/X/W | 内核访问需手工翻译 |
| 用户栈 | VA=PA（固定/预置） | VA=用户空间地址 → PA=分配页，经 user 页表映射 | U，RW | lab2_3 可按缺页增长 |
| 用户页表根 | 未使用 | `satp(user)` 指向用户页表根（物理页） | S 持有/U 不可见 | 进出用户态切换 `satp` |
| 共享/公共页（如陷入向量页） | 直接位于内核空间 | 在内核页表映射，必要时在用户不可见 | 主要 S 可见 | 保证陷入/返回路径可用 |

补充要点
- 页大小: 4KB；SV39 三级页表（lab2_1）。
- `satp`: 切到用户态前写入 `satp(user)`，陷入后先切回 `satp(kernel)`；内核访问用户指针需走软件翻译（如 `user_va_to_pa`）。
- 地址示例: 内核起始 0x80000000；lab1 示例应用入口 0x81000000（由 `user.lds`）。

### 应用进程映射方式
#### 1. 用户空间（低地址）
这是应用程序自己“看”到的、可以自由使用的内存区域。

*   **代码段/数据段 (Code/Data)**
    *   **地址**：通常从 `0x10000` (64KB) 开始。
    *   **来源**：这是您编译出的 ELF 可执行文件（如 `app_helloworld`）的内容。
    *   **映射**：内核读取 ELF 文件，分配物理页，将这些物理页映射到虚拟地址 `0x10000` 处。
    *   **权限**：用户可读、可执行（R/X）。

*   **用户栈 (User Stack)**
    *   **地址**：固定在 `0x7ffff000` (约 2GB 处) 以下。
    *   **来源**：内核在加载程序时，专门分配了一个物理页作为栈。
    *   **映射**：将这个物理页映射到虚拟地址 `0x7ffff000`。
    *   **权限**：用户可读、可写（R/W）。

**“低地址”的含义**：相对于 64 位系统的巨大空间，或者相对于内核所在的 `0x80000000` 以上地址，`0x10000` 和 `0x7ffff000` 都属于较低的地址范围。

#### 2. 内核空间（高地址）
这部分虽然在**用户页表**里有映射，但**用户程序不能直接访问**（因为页表项的 User 权限位是 0）。它们存在于用户页表中的目的是为了**处理中断和异常**。

*   **Trapframe (中断帧)**
    *   **地址**：通常在 `0x8xxxxxxx` (物理内存直接映射区域)。
    *   **作用**：当发生系统调用或中断时，CPU 需要一个地方保存用户程序的寄存器（上下文）。
    *   **为什么在用户页表里？**：当 CPU 刚进入内核态（S 态）但还未切换页表（`satp` 仍指向用户页表）时，代码需要立即保存寄存器。如果用户页表里没有映射这块内存，CPU 就无法写入数据，导致崩溃。

*   **Trap Vector (中断向量代码)**
    *   **地址**：也在 `0x8xxxxxxx` 区域。
    *   **作用**：这是处理中断的第一段汇编代码（`smode_trap_vector`）。
    *   **为什么在用户页表里？**：同理，当发生中断时，CPU 会跳转到这个地址执行指令。如果用户页表里没有映射这段代码，CPU 就会取指失败（Page Fault）。

#### 总结图示

```text
      虚拟地址 (Virtual Address)              物理地址 (Physical Address)
      -------------------------              ---------------------------
 高   0x8xxxxxxx  [Trap Vector]  ---映射--->  0x8xxxxxxx (内核代码)
      0x8xxxxxxx  [Trapframe]    ---映射--->  0x8xxxxxxx (内核数据)
      ...
      ... (中间巨大的未映射空洞)
      ...
      0x7ffff000  [User Stack]   ---映射--->  0x8xxxxxxx (某分配的物理页)
 低   0x00010000  [User Code]    ---映射--->  0x8xxxxxxx (某分配的物理页)
```

**核心逻辑**：
用户程序只能在“低地址”玩耍。一旦发生中断（如 `ecall`），CPU 跳转到“高地址”的内核代码（Trap Vector），并使用“高地址”的数据区（Trapframe）保存现场，然后再切换到真正的内核页表。

---

# lab2_1

### 1. 问题分析：为什么程序会崩溃？

首先，我们需要理解实验初始状态下，程序为什么无法正常打印 "Hello world!" 并崩溃。

1.  **执行流程**：
    *   `app_helloworld_no_lds.c` 中的 `main` 函数调用 `printu("Hello world!\n")`。
    *   `printu` 是一个库函数，它会准备好系统调用号和参数，然后执行 `ecall` 指令，从用户模式（U-mode）陷入到监管者模式（S-mode），也就是PKE内核中。
    *   内核的 `trap_handler` (位于 `kernel/strap.c`) 会接管，并根据系统调用号，最终调用 `sys_user_print` 函数 (位于 `kernel/syscall.c`)。

2.  **地址空间隔离**：
    *   **关键点**：Lab 2 开启了Sv39页式虚拟内存管理。这意味着**用户进程**和**操作系统内核**拥有各自独立的虚拟地址空间和页表。
    *   `printu` 传递给内核的字符串地址（我们称之为 `buf`）是一个**用户虚拟地址**。从实验的运行日志可以看出，用户代码段的虚拟地址从 `0x10000` 开始，所以 "Hello world!" 字符串的虚拟地址也在这个很低的地址范围内。
    *   内核运行在自己的地址空间中，它对 `0x80000000` 以下的地址没有任何映射（参考图4.4）。当内核中的 `sys_user_print` 函数试图直接访问 `buf` 这个低地址时，MMU会发现内核的页表中没有这个地址的有效映射，从而产生一个缺页异常（Page Fault）。但在我们的PKE内核中，更直接的原因是我们根本没有去访问它，而是先尝试转换它。

3.  **代码定位**：
    我们查看 `kernel/syscall.c` 中的 `sys_user_print` 函数：
    ```c
    21 ssize_t sys_user_print(const char* buf, size_t n) {
    22   //buf is an address in user space on user stack,
    23   //so we have to transfer it into phisical address (kernel is running in direct mapping).
    24   assert( current );
    25   char* pa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)buf);
    26   sprint(pa);
    27   return 0;
    28 }
    ```
    第25行代码明确地尝试将用户虚拟地址 `buf` 转换为物理地址 `pa`。这个转换是通过调用 `user_va_to_pa` 函数完成的。
    接着看 `kernel/vmm.c` 中 `user_va_to_pa` 的初始实现：
    ```c
    150 void *user_va_to_pa(pagetable_t page_dir, void *va) {
    ...
    160   panic( "You have to implement user_va_to_pa (convert user va to pa) to print messages in lab2_1.\n" );
    161 }
    ```
    显然，函数直接调用了 `panic`，导致系统停机并打印出错误信息。这就是我们在初始运行结果中看到 `You have to implement user_va_to_pa ...` 的原因。

我们的任务就是实现 `user_va_to_pa`，让它能正确地完成地址转换。

### 2. 解题思路：如何实现地址转换？

地址转换的本质是模拟硬件MMU（内存管理单元）查询页表的过程。根据 Sv39 的三级页表结构（图4.1），这个过程如下：

1.  从 `satp` 寄存器获取根页表（Page Directory）的物理地址。在我们的函数中，这个地址已经通过参数 `page_dir` 传入了。
2.  从虚拟地址 `va` 中提取最高9位的 VPN[2]，用它作为索引在根页表中找到对应的页目录项（PDE）。
3.  检查这个PDE是否有效（Valid位为1）。如果无效，转换失败。
4.  如果有效，从PDE中提取下一级页表（Page Medium Directory）的物理地址。
5.  从虚拟地址 `va` 中提取中间9位的 VPN[1]，用它作为索引在第二级页表中找到PDE。
6.  检查这个PDE是否有效。如果无效，转换失败。
7.  如果有效，从PDE中提取最后一级页表（Page Table）的物理地址。
8.  从虚拟地址 `va` 中提取最低9位的 VPN[0]，用它作为索引在末级页表中找到页表项（PTE）。
9.  检查这个PTE是否有效。如果无效，转换失败。
10. 如果有效，我们找到了最终的映射！从PTE中提取出物理页号（PPN），将其左移12位得到**物理页的基地址**。
11. 从虚拟地址 `va` 中提取最低12位的页内偏移（offset）。
12. **最终物理地址 = 物理页基地址 + 页内偏移**。

这个过程比较繁琐。幸运的是，实验代码已经为我们提供了一个完美的辅助函数 `page_walk`，它封装了上述步骤1到步骤8。

#### 利用 `page_walk`

我们来看一下 `page_walk` 的函数签名 (在 `kernel/vmm.c` 中)：
`pte_t *page_walk(pagetable_t page_dir, uint64 va, int alloc);`

*   `page_dir`: 根页表的地址。
*   `va`: 要查找的虚拟地址。
*   `alloc`: 一个标志。如果为 `1`，在查找过程中如果发现某一级页表不存在，就会分配一个新的物理页作为下一级页表。如果为 `0`，则遇到不存在的页表时直接返回 `NULL`。

在地址翻译的场景下，我们只是查询已有的映射，而不创建新的映射，所以 `alloc` 参数应该传 `0`。

`page_walk` 会返回一个指向**末级页表项（PTE）的指针**。有了这个PTE，我们就可以完成剩下的工作。

#### 完整实现步骤

1.  **调用 `page_walk`**：
    使用 `page_walk(page_dir, va, 0)` 来获取虚拟地址 `va` 对应的PTE的指针。

2.  **检查 `page_walk` 的返回值**：
    *   如果返回 `NULL`，说明在遍历页表的过程中，某个中间页表不存在。这意味着该虚拟地址没有被映射。函数应返回 `NULL`。
    *   如果返回一个有效的指针 `pte`，我们得到了PTE本身。

3.  **检查PTE的有效性**：
    即使 `page_walk` 返回了PTE的地址，这个PTE本身可能被标记为无效（`V` 位为0）。我们需要检查 `*pte & PTE_V`。
    *   如果 `V` 位为0，说明映射无效。函数应返回 `NULL`。

4.  **（可选但推荐）进行安全检查**：
    这是一个从用户空间到内核的调用，我们要翻译的是用户地址。因此，这个地址对应的物理页必须是用户可以访问的（`U` 位为1）。我们需要检查 `*pte & PTE_U`。
    *   如果 `U` 位为0，说明这是一个内核页，用户不应该能通过这个地址访问它，这是一种潜在的安全问题。函数应返回 `NULL`。

5.  **计算物理地址**：
    如果所有检查都通过，我们就可以计算最终的物理地址了。
    *   **提取物理页基地址**：PTE的高44位是物理页号（PPN）。我们可以使用 `riscv.h` 中定义的宏 `PTE2PA(*pte)` 来直接从PTE中提取出4KB对齐的物理页基地址。
    *   **提取页内偏移**：虚拟地址的低12位就是页内偏移。可以通过 `(uint64)va & (PGSIZE - 1)` 来获得。(`PGSIZE` 是 4096，`PGSIZE - 1` 就是 `0xFFF`)。
    *   **合并**：将物理页基地址和页内偏移相加，就得到了最终的物理地址。

6.  **返回结果**：
    将计算出的 `uint64` 类型的物理地址强制转换为 `void *` 类型并返回。

### 3. 代码实现

根据以上思路，我们在 `kernel/vmm.c` 中填充 `user_va_to_pa` 函数。

```c
// in kernel/vmm.c

#include "riscv.h" // 确保包含了PTE_V, PTE_U, PTE2PA等宏定义

void *user_va_to_pa(pagetable_t page_dir, void *va) {
  // TODO (lab2_1): implement user_va_to_pa to convert a given user virtual address "va"
  // to its corresponding physical address, i.e., "pa". To do it, we need to walk
  // through the page table, starting from its directory "page_dir", to locate the PTE
  // that maps "va". If found, returns the "pa" by using:
  // pa = PYHS_ADDR(PTE) + (va - va & (1<<PGSHIFT -1))
  // Here, PYHS_ADDR() means retrieving the starting address (4KB aligned), and
  // (va - va & (1<<PGSHIFT -1)) means computing the offset of "va" in its page.
  // Also, it is possible that "va" is not mapped at all. in such case, we can find
  // invalid PTE, and should return NULL.
  
  uint64 uva = (uint64)va;

  // 1. 使用 page_walk 查找 va 对应的末级页表项(PTE)
  //    第二个参数为0，表示如果找不到映射，不要创建新的页表
  pte_t *pte = page_walk(page_dir, uva, 0);

  // 2. 检查 page_walk 的返回值
  //    如果 pte 为 NULL，表示中间页表不存在，映射无效
  if (pte == NULL) {
    return NULL;
  }

  // 3. 检查 PTE 的有效位 (Valid bit)
  //    如果 V 位为 0，表示该页表项无效
  if ((*pte & PTE_V) == 0) {
    return NULL;
  }

  // 4. 安全检查：检查 PTE 的用户位 (User bit)
  //    我们要翻译的是用户地址，所以它必须指向一个用户页
  if ((*pte & PTE_U) == 0) {
    return NULL;
  }

  // 5. 计算物理地址
  //    PTE2PA(*pte) 从PTE中提取物理页的基地址
  //    uva & (PGSIZE - 1) 计算页内偏移
  uint64 pa = PTE2PA(*pte) + (uva & (PGSIZE - 1));

  return (void *)pa;
}
```

完成以上代码后，重新 `make clean; make` 并运行，`sys_user_print` 就能成功将用户虚拟地址转换为物理地址，然后 `sprint` 就能根据物理地址找到 "Hello world!\n" 字符串并正确打印。
完美！日志清楚地证明了整个过程。让我根据实际运行结果做最终总结：


### 一些疑问
#### **1）为什么 buf 地址需要手动转换？**

**答案：因为 `satp` 指向的页表不匹配**

```
用户态访问 stack_var:
  satp = 0x8000000000087fbb (用户页表)
  VA = 0x7fffefec
  MMU 查用户页表 → 自动找到映射 → 成功 ✅

内核态访问 buf:
  satp = 0x8000000000087ffe (内核页表) ← 关键差异！
  VA = 0x7fffee88 (仍是用户虚拟地址)
  MMU 查内核页表 → 找不到 0x7fff... 的映射 → 失败 ❌
  
解决方案：
  user_va_to_pa(0x87fbb000, buf) ← 手动查用户页表
  → PA = 0x87fb9e88 → 成功 ✅
```

**核心原因**：MMU 硬件只会用当前 `satp` 指向的页表，不会自动切换或同时查两个页表。



#### **2）satp 切换是如何发生的？**

**答案：在汇编代码中通过 `csrw satp` 指令完成**

##### **切换时机 1：进入用户态**
```assembly
# kernel/strap_vector.S: 64-66 行
return_to_user:
    csrw satp, a1          # 写入用户页表地址
    sfence.vma zero, zero  # 刷新 TLB
    ...
    sret                   # 返回用户态
```

**调用路径**：
```
kernel/process.c: switch_to()
  ↓ 计算 user_satp = 0x8000000000087fbb
  ↓ 调用 return_to_user(trapframe, user_satp)
  ↓
kernel/strap_vector.S: return_to_user
  ↓ csrw satp, a1  ← 切换！
```

##### **切换时机 2：陷入内核态**
```assembly
# kernel/strap_vector.S: 46-47 行
smode_trap_vector:
    ...
    ld t1, 272(a0)         # 从 trapframe->kernel_satp 加载
    csrw satp, t1          # 写入内核页表地址
    sfence.vma zero, zero  # 刷新 TLB
    jr t0                  # 跳转到 C 处理函数
```

**触发路径**：
```
用户态执行 ecall
  ↓ 硬件自动跳转到 stvec (smode_trap_vector)
  ↓
kernel/strap_vector.S: smode_trap_vector
  ↓ 保存用户寄存器
  ↓ csrw satp, t1  ← 切换！
  ↓ 跳转到 smode_trap_handler()
```

**日志证据**：
```
Before: satp = 0x8000000000087ffe (kernel)
After:  satp = 0x8000000000087fbb (user)   ← 切换发生
```


#### **3）哪里显式地为用户态和内核态分配了不同的页表？**

**答案：在内核初始化和进程创建时**

##### **① 内核页表分配**
```c
// kernel/kernel.c: 89-92 行
pmm_init();       // 初始化物理内存管理器
kern_vm_init();   // 创建内核页表
enable_paging();  // 启用分页，写 satp
```

```c
// kernel/vmm.c: kern_vm_init()
void kern_vm_init() {
  pagetable_t t_page_dir = (pagetable_t)alloc_page();  // 分配页表
  memset(t_page_dir, 0, PGSIZE);
  
  // 映射内核代码、数据等
  kern_vm_map(t_page_dir, KERN_BASE, ...);
  kern_vm_map(t_page_dir, (uint64)_etext, ...);
  
  g_kernel_pagetable = t_page_dir;  // 保存内核页表 ← 关键！
}
```

```c
// kernel/kernel.c: enable_paging()
void enable_paging() {
  write_csr(satp, MAKE_SATP(g_kernel_pagetable));  // 启用内核页表
  flush_tlb();
}
```

##### **② 用户页表分配**
```c
// kernel/kernel.c: 48-51 行
void load_user_program(process *proc) {
  proc->trapframe = (trapframe *)alloc_page();
  proc->pagetable = (pagetable_t)alloc_page();  // 分配用户页表 ← 关键！
  memset((void *)proc->pagetable, 0, PGSIZE);
  
  // 装载 ELF 时建立映射
  load_bincode_from_host_elf(proc);  
    ↓ elf_alloc_mb() 
    ↓ user_vm_map(proc->pagetable, ...)  // 建立用户代码段映射
  
  // 映射用户栈
  user_vm_map(proc->pagetable, USER_STACK_TOP - PGSIZE, ...);
}
```

##### **③ 两个页表的关联**
```c
// kernel/process.c: 42 行
proc->trapframe->kernel_satp = read_csr(satp);  // 保存内核页表地址
```

**数据结构关系**：
```
全局变量:
  g_kernel_pagetable = 0x87ffe000  ← 内核页表根地址

进程结构:
  user_app.pagetable = 0x87fbb000  ← 用户页表根地址
  user_app.trapframe->kernel_satp = MAKE_SATP(g_kernel_pagetable)
```


#### 🎯 **核心流程总结**

```
启动阶段:
  kern_vm_init()
    → 分配 g_kernel_pagetable = 0x87ffe000
    → 映射内核地址空间
  
  load_user_program()
    → 分配 user_app.pagetable = 0x87fbb000
    → 映射用户地址空间

运行阶段:
  switch_to() 
    → return_to_user(user_satp=0x87fbb)
    → csrw satp, 0x87fbb  ← 切换到用户页表
    → sret (进入用户态)
  
  用户态执行:
    → 所有内存访问用 satp=0x87fbb 自动转换 ✅
  
  ecall (系统调用):
    → smode_trap_vector
    → csrw satp, 0x87ffe  ← 切换回内核页表
    → smode_trap_handler()
  
  内核态处理:
    → satp=0x87ffe，无法自动转换用户地址
    → 必须调用 user_va_to_pa(0x87fbb, buf) 手动查询 ✅
```

#### ✨ **一句话总结**

**两个独立的页表在不同阶段分配（`kern_vm_init` 和 `load_user_program`），通过汇编指令 `csrw satp` 在用户态/内核态之间切换，导致 MMU 只能用当前 `satp` 指向的页表自动转换地址，因此内核访问用户地址时必须手动查询用户页表。**



### 三个核心栈的解析

当一个用户应用程序（单线程）在PKE内核上运行时，以下三个栈是其生命周期中至关重要的：

#### 1. 用户态栈 (User Stack)

*   **作用域**：用户模式 (U-Mode)
*   **用途**：这是应用程序自身执行时使用的栈。函数调用、局部变量、参数传递等都发生在这个栈上。
*   **来源**：在`load_user_program()`函数中为进程分配。文档中提到：
    ```c
    // kernel/kernel.c in lab2
    50   uint64 user_stack = (uint64)alloc_page();       // 为用户栈分配物理页面
    53   proc->trapframe->regs.sp = USER_STACK_TOP;      // 设置用户栈顶的虚拟地址
    ```
    这个栈位于用户进程的虚拟地址空间中（例如，栈顶在`0x7ffff000`），与内核完全隔离。

#### 2. 进程的内核栈 ("用户内核栈", Process's Kernel Stack)

*   **作用域**：监管者模式 (S-Mode)，在处理来自特定进程的陷阱(trap)时使用。
*   **用途**：当用户程序通过`ecall`（系统调用）、或触发异常、或被外部中断打断时，CPU会从U-Mode切换到S-Mode。为了处理这个陷阱，内核**不会使用用户栈**，而是切换到这个为该进程专门准备的内核栈。这样做可以：
    1.  **安全隔离**：防止内核受到用户栈的恶意破坏（如栈溢出）。
    2.  **上下文独立**：每个进程都有自己独立的内核栈，使得在多任务环境下，一个进程在内核中被阻塞时，其内核上下文（函数调用链）可以被完整地保存在自己的内核栈上，而不会影响其他进程。
*   **来源**：同样在`load_user_program()`中为进程分配，但这个栈是内核可以直接访问的物理地址，不会映射到用户虚拟地址空间。
    ```c
    // kernel/kernel.c in lab2
    49   proc->kstack = (uint64)alloc_page() + PGSIZE;   // 分配并设置进程内核栈的栈顶
    ```
    在`smode_trap_vector`中，会执行从用户栈到这个栈的切换。

#### 3. 机器模式栈 (M-mode Stack)

*   **作用域**：机器模式 (M-Mode)
*   **用途**：用于处理**最高权限**的事件。在PKE中，这包括：
    1.  **未代理给S模式的异常**：如`lab1_2`中的非法指令异常 (`CAUSE_ILLEGAL_INSTRUCTION`)。
    2.  **外部中断的初始捕获**：如`lab1_3`中的时钟中断 (`Timer Interrupt`)。中断首先在M-Mode被捕获，然后M-Mode通过写`sip`寄存器的方式，向S-Mode发出一个“软件中断”，将处理权“接力”给S-Mode。
*   **来源**：这个栈是内核启动时静态分配的，通常称为`stack0`。在`_mentry`中被设置，供所有M-Mode下的陷阱处理程序使用。
    ```assembly
    // kernel/machine/mentry.S
    20     la sp, stack0       # stack0 is statically defined in kernel/machine/minit.c
    ...
    28     call m_start
    ```

---

# lab2_3
## 异常处理路径（从触发到恢复）

### 1. **异常触发** → CPU 硬件自动处理

```
用户程序执行非法操作（如访问未映射地址、非法指令、ecall）
  ↓
CPU 检测到异常
  ↓
CPU 自动操作：
  - 保存当前 PC 到 sepc（或 mepc，取决于异常级别）
  - 保存异常原因到 scause（或 mcause）
  - 保存出错地址到 stval（或 mtval）
  - 切换特权级（U→S 或 S→M）
  - 跳转到 stvec（或 mtvec）指向的处理入口
```


### 2. **陷入处理入口** → 汇编代码（Kernel）

#### S 模式异常（系统调用、缺页）
```
CPU 跳转到 smode_trap_vector (kernel/strap_vector.S)
  ↓
保存所有寄存器到 trapframe
切换 satp 到内核页表
切换栈到 proc->kstack
  ↓
调用 smode_trap_handler() (C 函数)
```

#### M 模式异常（非法指令、时钟中断）
```
CPU 跳转到 mtrapvec (kernel/machine/mtrap_vector.S)
  ↓
保存所有寄存器到 g_itrframe
切换栈到 stack0
  ↓
调用 handle_mtrap() (C 函数)
```


### 3. **分发处理** → Kernel C 代码

#### S 模式：`smode_trap_handler()` (kernel/strap.c)
```c
读取 scause 判断异常类型
  ↓
switch (scause):
  case CAUSE_USER_ECALL:
    → handle_syscall() → do_syscall()
  case CAUSE_STORE_PAGE_FAULT:
    → handle_user_page_fault() → 分配页并映射
  case CAUSE_MTIMER_S_TRAP:
    → handle_mtimer_trap() → 增加 ticks
  ↓
调用 switch_to(current) 准备返回用户态
```

#### M 模式：`handle_mtrap()` (kernel/machine/mtrap.c)
```c
读取 mcause 判断异常类型
  ↓
switch (mcause):
  case CAUSE_ILLEGAL_INSTRUCTION:
    → handle_illegal_instruction() → panic()
  case CAUSE_MTIMER:
    → handle_timer() → 设置 SIP_SSIP 转发给 S 态
  ↓
mret 返回（若不 panic）
```


### 4. **返回用户态** → 汇编代码 + CPU

```
switch_to() 设置 trapframe 各字段
  ↓
调用 return_to_user (kernel/strap_vector.S)
  ↓
恢复 trapframe 中的寄存器
写 satp = 用户页表
  ↓
sret (CPU 指令)
  ↓
CPU 自动操作：
  - 从 sepc 恢复 PC
  - 切换特权级（S→U）
  - 继续执行用户程序
```


### 完整流程图

```
[用户态] 触发异常
    ↓ (CPU 硬件)
[汇编] smode_trap_vector / mtrapvec
    ↓ 保存上下文、切换栈/页表
[C代码] smode_trap_handler / handle_mtrap
    ↓ 分发到具体处理函数
[C代码] handle_syscall / handle_user_page_fault / handle_timer 等
    ↓ 执行实际处理逻辑
[C代码] switch_to(current)
    ↓ 设置返回状态
[汇编] return_to_user
    ↓ 恢复寄存器、切换页表
[CPU] sret / mret
    ↓ 硬件恢复特权级和 PC
[用户态] 继续执行
```

### 关键点

| 阶段 | 执行者 | 职责 |
|------|--------|------|
| 异常触发 | **CPU 硬件** | 保存现场（sepc/scause/stval），跳转入口 |
| 入口处理 | **Kernel 汇编** | 保存寄存器，切换栈/页表 |
| 分发+处理 | **Kernel C 代码** | 判断异常类型，执行具体逻辑 |
| 返回准备 | **Kernel 汇编** | 恢复寄存器，写 satp |
| 返回执行 | **CPU 硬件** | 恢复 PC 和特权级（sret） |

**简而言之**：CPU 负责"硬件动作"（保存/恢复、跳转），Kernel 负责"软件逻辑"（判断、分配、映射）。
