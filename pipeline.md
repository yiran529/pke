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

## Lab1_challenge2 速记总结：异常行号与源码输出

### 关键疑问与答案
- **用哪个寄存器拿到出错PC？** M态读 `mepc`，S态读 `sepc`。
- **PC 如何映射到源行？** 用 `.debug_line` 生成的三张表：`dir`(目录数组)、`file`(文件名+目录索引)、`line`(PC→行号/文件索引)。在线表里找 `line[i].addr <= pc < line[i+1].addr`，得行号与文件索引，再拼路径。
- **为何改 ELF 装载？** 无 `kmalloc` 且需附加元数据空间；用 `image_end` 在用户镜像尾部预留并放置 `.debug_line`+`dir/file/line`。
- **如何打印源码文本？** 通过 hostfs：`spike_file_open(fullpath)` 打开宿主机源码，逐行读到目标行后 `sprint` 输出。
- **运行参数常犯错？** `spike` 第二个参数必须是编译后的 ELF，如 `./obj/app_loadaccess_error`，不能传 `.c`。

### 实现要点
1) **ELF 装载改造**（`kernel/elf.c`, `kernel/elf.h`）
    - 新增 `elf_ctx.image_end` 记录用户镜像已用最高 VA。
    - `elf_load` 跟踪各段末尾并对齐更新 `image_end`。
    - `reserve_loader_space` 在镜像尾部分配对齐空间；`load_debug_line_section` 读取节头字符串表，定位 `.debug_line`，连同 `dir/file/line` 元数据一起放到尾部后调用 `make_addr_line`。

2) **行号查找与源码打印**（`kernel/machine/mtrap.c`）
    - 异常时读 `mepc`（或 `sepc`）。
    - 线性扫描 `current->line` 找命中项，打印路径+行号。
    - 用 `strcpy/strcat` 组装 `fullpath`，`spike_file_open/read` 逐行读到目标行打印，再 `spike_file_close`。

3) **字符串工具**（`util/string.c/h`）
    - 补全 `strcat` 以便路径拼接。

4) **运行示例**
    ```bash
    make && spike ./obj/riscv-pke ./obj/app_loadaccess_error
    ```
    若提示 “Fail on openning the input application program.”，检查是否传入 `.c` 而非 `.elf`。

5) **你需要理解ELF的结构**
- ELF head
- section header table
- section header
- program header
除此之外，你需要知道如何利用这些信息获得指定名字的section的信息

### 常见坑位
- `.debug_line` 需要额外元数据空间，缺失会越界；用 `image_end`+对齐分配解决。
- 源码只在宿主机文件系统中，用户镜像内无源码，必须用 hostfs 打开。
- 查 `line` 表要注意边界：用 `<=` / `<` 组合防止错行。

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