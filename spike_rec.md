**`frontend_syscall` 的作用**

PKE 运行在 Spike 模拟的 RISC-V 机器上，该机器本身没有真正的磁盘/键盘等硬件。Spike 提供了 HTIF（Host-Target Interface）机制，让目标机（RISC-V）可以"借用"宿主机（运行 Spike 的 Linux）的能力。

`frontend_syscall` 就是向宿主机发起一次 HTIF 系统调用的入口：

```
PKE内核  →  frontend_syscall(HTIFSYS_xxx, ...)
                    ↓  通过 HTIF 寄存器
           Spike 拦截并转发给宿主机 Linux
                    ↓
           宿主机执行真正的系统调用（open/read/getdents64等）
                    ↓  结果原路返回
PKE内核  ←  返回值
```

`spike_file_read`/`spike_file_open` 等函数本质上都是对 `frontend_syscall` 的封装，只是没有封装所有系统调用（比如 `getdents64`、`mkdirat`），所以才需要直接调用它。

---

**`struct dir` 表示什么**

```c
// util/types.h
struct dir {
  char name[MAX_FILE_NAME_LEN];  // 条目名称
  int inum;                       // inode 编号
};
```

它实际上是一个**目录项（directory entry）**，表示"某个目录下的一个条目"。这个条目**可以是普通文件，也可以是子目录**——它只记录名字和 inode 号，不区分类型。

类比关系：
| hostfs/rfs 中的概念 | 对应 Linux 中的概念 |
|---|---|
| `struct dir` | `struct dirent`（`readdir` 返回的单条目） |
| `vfs_readdir` | `readdir()` / `getdents64()` |

所以 `struct dir` 的命名有些误导——它是"目录中的一个条目"而不是"目录本身"，普通文件的目录项完全可以用它来表示。