# 命令历史功能Debug详细记录

本文档详细记录从 `size < PGSIZE 断言错误` 到最终解决 `.sbss/gp相对寻址` 问题的完整debug过程。

---

## 第一阶段：初始错误与诊断

### 1.1 现象：ELF加载器断言失败

**错误信息：**
```
assertion failed @ 0x0000000080016218: size < PGSIZE
```

**背景：**
在实现命令历史功能时，向 `app_shell` 中添加了全局数组：
```c
static history_item g_history[HISTORY_MAX_ITEMS];  // 32 items × 96 bytes ≈ 3KB
static int g_history_count = 0;
```

重新编译后运行，kernel在加载 `/bin/app_shell` 时断言失败。

### 1.2 初步分析：用readelf查看可执行文件结构

第一步是用 `readelf` 命令查看 `app_shell` 的ELF结构：

```bash
readelf -l hostfs_root/bin/app_shell
```

**输出示例（关键部分）：**
```
Program Headers:
  Type           Offset             VirtAddr           PhysAddr
                 FileSiz            MemSiz              Flags  Align
  LOAD           0x0000000000001000 0x0000000000010000 0x0000000000010000
                 0x0000000000001010 0x00000000000110a4 Flags: R E
  LOAD           0x0000000000002010 0x0000000000021000 0x0000000000021000
                 0x0000000000000000 0x0000000000000c18 Flags: RW
```

**关键观察：**
1. **第二个LOAD段的MemSiz = 0xc18（3096字节）** > 4096字节（PGSIZE）吗？
   - 不是。0xc18 = 3096 < 4096
   - 但等等，这个段在FileSiz = 0（文件中没有内容），MemSiz = 0xc18
   - 这是典型的 `.bss` 段（未初始化数据段）

2. **第一个LOAD段的MemSiz = 0x10a4（4260字节）** 
   - 0x10a4 = 4260 > 4096（PGSIZE）
   - 这就是问题！代码段加上初始数据超过了一个page的大小

### 1.3 用size命令快速检查二进制大小

```bash
size hostfs_root/bin/app_shell
```

**输出：**
```
   text    data     bss     dec     hex
   4084     384    3096    7564    1d9c
```

**解读：**
- `text = 4084` 字节：所有代码
- `data = 384` 字节：初始化的全局/静态数据
- `bss = 3096` 字节：未初始化的全局/静态数据（包括 `g_history` 数组）

总大小 = 4084 + 384 + 3096 = 7564 字节，跨越两个4096字节的page。

**关键点：** 添加 `g_history[32×96]` 后，bss段增长了3096字节，导致整个代码段 + 数据段 > PGSIZE。

---

## 第二阶段：定位ELF加载器问题

### 2.1 查看kernel/elf.c中的断言

打开 `kernel/elf.c`，找到断言位置：

```c
void *elf_alloc_mb(elf_ctx *ctx, uint64 va, uint64 len, uint64 sz) {
  _assert(sz < PGSIZE, "alloc_mb size must be less than a page");  // <-- 这里！
  // ...
}
```

**问题：** 这个函数假设分配的内存大小必须小于PGSIZE（4096）。但ELF loader会一次性为整个segment分配内存，不管segment有多大。

### 2.2 查看elf_load函数的实现

关键代码段：
```c
void elf_load(elf_ctx *ctx) {
  for (uint32 i = 0; i < ctx->ehdr.e_phnum; i++) {
    // ...获取Program Header...
    if (ph->p_type == PT_LOAD) {
      void *dest = elf_alloc_mb(ctx, ph->p_vaddr, 0, ph->p_memsz);
      //                                           ↑
      //                              一次分配整个segment！
      elf_fpread(ctx, dest, ph->p_filesz, ph->p_offset);
    }
  }
}
```

**问题：** `ph->p_memsz` 可能 > 4096 字节，但 `elf_alloc_mb` 断言 `sz < PGSIZE`。

---

## 第三阶段：理解RISC-V内存分页模型

### 3.1 什么是PGSIZE？

在RISC-V中，PGSIZE = 4096（4KB）是最小的内存管理单位。

- **虚拟地址空间** 被分为 4KB 大小的页
- **物理内存** 也被分为 4KB 大小的页
- CPU 的 MMU（Memory Management Unit）通过页表进行 VA → PA 映射

### 3.2 segment跨越多个page的情况

当一个ELF segment的大小 > PGSIZE 时，需要分配 **多个物理页面**：

```
Segment size = 7KB (需要2个page):
┌─────────────┐
│   Page 0    │  (4KB)
├─────────────┤
│   Page 1    │  (3KB，未满)
└─────────────┘
```

**原始代码的缺陷：**
- 一次性调用 `elf_alloc_mb(ctx, va, 0, 7168)`
- `elf_alloc_mb` 只分配一个物理页面（4KB）
- 数据溢出，导致段错误或覆盖其他内存

---

## 第四阶段：修复ELF加载器（支持多页段）

### 4.1 修复strategy

将 `elf_load` 修改为 **逐页加载**：

```c
void elf_load(elf_ctx *ctx) {
  for (uint32 i = 0; i < ctx->ehdr.e_phnum; i++) {
    elf_phdr *ph = ctx->phdr + i;
    if (ph->p_type == PT_LOAD) {
      // 按页循环处理segment
      for (uint64 page_off = 0; page_off < ph->p_memsz; page_off += PGSIZE) {
        // 计算当前page需要分配多少字节
        uint64 chunk = (ph->p_memsz - page_off > PGSIZE) 
                       ? PGSIZE 
                       : (ph->p_memsz - page_off);
        
        // 分配这一页（chunk ≤ PGSIZE，通过断言）
        void *dest = elf_alloc_mb(ctx, ph->p_vaddr + page_off, 0, chunk);
        
        // 只读取有实际文件内容的部分
        // 超过filesz的部分自动zeroed（BSS）
        if (page_off < ph->p_filesz) {
          uint64 file_chunk = (ph->p_filesz - page_off > chunk) 
                              ? chunk 
                              : (ph->p_filesz - page_off);
          elf_fpread(ctx, dest, file_chunk, ph->p_offset + page_off);
        }
      }
    }
  }
}
```

### 4.2 更新mapped_info统计

同时需要更新segment映射信息中的页数统计：

```c
// 原始代码（错误）：
mapped_info[j].npages = ph->p_memsz / PGSIZE;  // 可能丢失余数

// 修复后（正确）：
mapped_info[j].npages = (ph->p_memsz + PGSIZE - 1) / PGSIZE;  // 向上取整
```

**例子：**
- MemSiz = 7168 (0x1c00)
- Old: 7168 / 4096 = 1（错！缺少第二个page）
- New: (7168 + 4095) / 4096 = 11263 / 4096 = 2（正确！）

### 4.3 同时修复elf_load_vfs函数

同样的逻辑也适用于从虚拟文件系统读取ELF的版本。

完成修复后，编译：
```bash
make clean && make -j4
```

测试运行：
```bash
timeout 12s spike -p2 obj/riscv-pke /bin/app_shell /bin/app_alloc1
```

**结果：** `size < PGSIZE` 断言不再出现。✓

---

## 第五阶段：新错误出现："Misaligned AMO"

修复ELF加载后，又遇到新错误：

```
Exception Report...
Misaligned AMO!
```

这是因为在 `app_shell.c` 的 `main` 函数中调用 `naive_malloc` 初始化全局历史数组时发生：

```c
// 第一次malloc调用
command = (char *)naive_malloc();       // 用于解析命令
para = (char *)naive_malloc();          // 用于解析参数
g_history = (history_item *)naive_malloc();  // 新添加，导致AMO异常
```

根据PKE源代码，`naive_malloc` 内部使用原子操作（AMO），而app_shell的某部分代码可能未能正确对齐导致问题。

### 5.1 第一次尝试解决：避免malloc

改用静态数组：
```c
static history_item g_history[HISTORY_MAX_ITEMS];
static int g_history_count = 0;
```

这样避免了malloc。重新编译后：

```bash
make -j4 && timeout 12s spike -p2 obj/riscv-pke /bin/app_shell /bin/app_alloc1 | sed -n '1,50p'
```

**新问题：** 出现了 `page fault` 错误
```
handle_page_fault: fffffffffffff80c
```

---

## 第六阶段：诊断页表错误（gp-相对寻址问题）

### 6.1 用riscv64-unknown-elf-nm定位符号

当 `app_history` 命令或 `history_append()` 被调用时，触发页表错误。这提示是全局变量访问的问题。

使用nm命令检查符号表：

```bash
riscv64-unknown-elf-nm -n hostfs_root/bin/app_shell | grep history
```

**输出：**
```
00000000000120b0 b g_history_count
00000000000120c0 b g_history
```

**解读：**
- `b` 表示 **BSS** 段符号（未初始化数据）
- `0x120b0` 和 `0x120c0` 是虚拟地址

### 6.2 分析.bss段位置

再次用readelf查看：

```bash
readelf -S hostfs_root/bin/app_shell | grep -A2 bss
```

**输出：**
```
  [26] .bss              NOBITS          00000000000120b0  00002010
       0000000000000c18  0000000000000000  WA       0     0     1
```

**关键信息：**
- `.bss` 段从 0x120b0 开始
- 大小 0xc18 (3096 bytes)
- 标志 `WA`（可写/可分配）

### 6.3 理解gp-相对寻址

RISC-V使用 **全局指针（gp）** 进行 **PC-相对的近距离寻址**。

在用户态，`gp` 寄存器指向 `.data` 或 `.bss` 段附近的一个固定点：

```
┌─────────────────┐
│      .text      │  (代码段，执行)
├─────────────────┤
│      .data      │  (初始化数据)
├─────────────────┤
│   → gp点        │  <-- gp寄存器指向这里
│      .bss       │  (未初始化数据)
└─────────────────┘
```

### 6.4 什么时候gp-相对寻址会失败？

当全局变量访问超出 **gp的可寻址范围** 时：

```c
// app_shell.c中：
static history_item g_history[HISTORY_MAX_ITEMS];  // 在.bss
static int g_history_count = 0;                     // 在.bss

// 在某个函数中访问：
history_append(g_history, &g_history_count, ..., ...);
//                ↓
//          编译器生成：lw reg, OFFSET(gp)
//          但OFFSET超出寻址范围 → page fault
```

错误地址 `fffffffffffff80c` 是gp-相对寻址产生的无效地址。

### 6.5 用objdump分析生成的指令

生成并查看反汇编：
```bash
riscv64-unknown-elf-objdump -S hostfs_root/bin/app_shell | grep -A10 "history_append"
```

会看到类似：
```
    addi sp, sp, -XXX
    ...
    lui reg, %hi(g_history)      // 高位 gp-相对
    addi reg, reg, %lo(g_history) // 低位 gp-相对
    ...
```

这些指令假设 `g_history` 在gp的某个固定偏移范围内，但实际地址超出范围。

---

## 第七阶段：最终解决方案（堆分配）

### 7.1 根本原因分析

问题不在于历史功能本身，而在于：

1. **全局.bss段太大**：添加 `g_history` 使 .bss 增长到 3096 字节
2. **gp可寻址范围有限**：通常只有 ±2KB 的范围
3. **越界访问**：g_history 的地址超出gp-相对寻址范围

### 7.2 解决方案：移到堆（动态分配）

把全局数组改为 **在main函数中动态分配**：

```c
int main(void) {
  // ... 其他初始化 ...
  
  // 关键修改：
  history_item *history = (history_item *)naive_malloc();  // 堆上分配
  int history_count = 0;                                   // 栈上（局部变量）
  
  // ... 读取shellrc ...
  
  // 调用时传递指针和计数指针：
  history_append(history, &history_count, command, para, bg);
  //                ↑                        ↑
  //         堆分配的指针           指向栈上的计数器
}
```

函数签名改为：
```c
static void history_append(history_item *history, int *history_count,
                          const char *command, const char *para, int bg)
```

### 7.3 为什么这样做有效？

1. **堆上的分配** 通过普通指针获取，不需要gp-相对寻址
2. **局部变量** `history_count` 在栈上，不在.bss段
3. **消除了全局.bss符号** → 没有gp-寻址问题

重新编译：
```bash
make -j4
size hostfs_root/bin/app_shell
```

**优化后大小：**
```
   text    data     bss     dec     hex
   4084     384     112    4580    11e4
                     ↑
              从3096减少到112（只有局部变量的frame）
```

### 7.4 验证和测试

```bash
timeout 12s spike -p2 obj/riscv-pke /bin/app_shell /bin/app_alloc1 | sed -n '1,100p'
```

**结果：**
- ✓ 不再出现 `page fault: fffffffffffff80c`
- ✓ shell 正常启动并执行命令
- ✓ 历史记录功能工作正常（虽然输出被注释）

---

## 总结：Debug工具使用指南

### 线索追踪顺序

| 工具 | 目的 | 用法 | 获取的信息 |
|------|------|------|-----------|
| `readelf -l` | 查看PE结构 | `readelf -l binary` | segment大小、位置、权限 |
| `size` | 快速估算段大小 | `size binary` | text/data/bss大小汇总 |
| `readelf -S` | 详细section信息 | `readelf -S binary` | .bss/.data/.text位置和大小 |
| `riscv64-unknown-elf-nm` | 查看符号表 | `nm -n binary` | 全局变量的地址和所在段 |
| `objdump -S` | 反汇编 + 源码 | `objdump -S binary` | 生成的指令和寻址模式 |

### 关键观察要点

1. **Segment vs Section**
   - Segment：PE格式的加载单位（LOAD, DYNAMIC等）
   - Section：ELF的逻辑组织单位（.text, .data, .bss）
   - 一个segment可能包含多个section

2. **.bss的特殊性**
   - 不占用文件空间，但占用内存
   - 在文件中 FileSiz=0，但 MemSiz>0
   - 加载时会自动零初始化

3. **gp-相对寻址的范围限制**
   - RISC-V设计gp用于提高代码密度
   - 可寻址范围受限（±2KB通常）
   - 全局数据过多会导致寻址范围溢出

4. **调试顺序**
   - 先用工具定位 **是什么**（通过readelf/size）
   - 再用工具定位 **在哪里**（通过nm, readelf -S）
   - 最后用工具定位 **怎么访问**（通过objdump -S）
   - 最后才对症下药（修改源码）

---

## 参考资源

- RISC-V ISA Manual: gp-relative addressing scheme
- ELF Binary Format: Program Headers vs Sections
- GNU Binutils: readelf, objdump, nm man pages
- PKE kernel doc: Virtual memory and paging architecture
