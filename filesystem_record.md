# RISCV-PKE 文件系统实现总结

## 一、整体架构

该项目采用 **VFS（虚拟文件系统）分层设计**，分为三层：

```
应用层（user/）
    ↓ 系统调用
进程文件管理层（proc_file）
    ↓ VFS接口
VFS抽象层（vfs）
    ↓ 操作接口
具体文件系统层（RFS/HostFS）
    ↓ 设备接口
设备层（ramdev/spike_file）
```

## 二、核心数据结构

### 2.1 VFS抽象层

#### **dentry（目录项）**
```c
struct dentry {
  char name[MAX_DENTRY_NAME_LEN];  // 目录/文件名（最多30字符）
  int d_ref;                        // 被打开文件引用计数
  struct vinode *dentry_inode;      // 指向对应的inode
  struct dentry *parent;            // 父目录指针
  struct super_block *sb;           // 所属文件系统超级块
};
```
**作用**：连接**路径名**和**inode**，构建文件系统目录树结构
- 内存缓存结构，加速路径查找
- 通过 `(parent, name)` 哈希索引

#### **vinode（虚拟inode）**
```c
struct vinode {
  int inum;                         // inode编号（磁盘标识）
  int ref;                          // 内存引用计数
  int size;                         // 文件大小（字节）
  int type;                         // 类型：FILE_I(0) 或 DIR_I(1)
  int nlinks;                       // 硬链接数（磁盘属性）
  int blocks;                       // 占用数据块数
  int addrs[DIRECT_BLKNUM];         // 直接块地址数组（10个）
  void *i_fs_info;                  // 文件系统特定信息
  struct super_block *sb;           // 所属超级块
  const struct vinode_ops *i_ops;   // 操作接口（多态）
};
```
**作用**：统一表示不同文件系统的文件/目录
- 内存中的inode表示，缓存磁盘元数据
- 通过 `(sb, inum)` 哈希索引
- 支持硬链接：多个dentry指向同一vinode

#### **file（打开文件描述符）**
```c
struct file {
  int status;                // FD_NONE(0) / FD_OPENED(1)
  int readable;              // 可读标志
  int writable;              // 可写标志
  int offset;                // 当前读写位置偏移
  struct dentry *f_dentry;   // 关联的目录项
};
```
**作用**：表示进程打开的文件实例
- 每个进程最多128个文件描述符
- 维护独立的读写偏移量

#### **super_block（超级块）**
```c
struct super_block {
  int magic;                // 文件系统魔数（0xBEAF）
  int size;                 // 文件系统大小（块数）
  int nblocks;              // 数据块总数
  int ninodes;              // inode总数
  struct dentry *s_root;    // 根目录项
  struct device *s_dev;     // 挂载的设备
  void *s_fs_info;          // FS特定信息（RFS位图/HostFS路径）
};
```
**作用**：文件系统元数据，每个挂载点一个

### 2.2 RFS文件系统层

#### **rfs_dinode（磁盘inode）**
```c
struct rfs_dinode {
  int size;                         // 文件大小
  int type;                         // R_FREE/R_FILE/R_DIR
  int nlinks;                       // 硬链接数
  int blocks;                       // 数据块数
  int addrs[RFS_DIRECT_BLKNUM];     // 直接块地址（10个）
};
```
**作用**：RFS文件系统在RAMDISK上的持久化inode格式（128字节）

#### **rfs_direntry（目录项）**
```c
struct rfs_direntry {
  int inum;                          // inode编号
  char name[RFS_MAX_FILE_NAME_LEN];  // 文件名（28字符）
};
```
**作用**：目录文件中的条目，记录子文件/子目录

#### **rfs_device（RAM磁盘设备）**
```c
struct rfs_device {
  void *d_address;      // RAMDISK基地址
  int d_blocks;         // 块数（默认128块）
  int d_blocksize;      // 块大小（4KB）
  void *iobuffer;       // IO缓冲区
  int (*d_write)(struct rfs_device *rdev, int blkno);
  int (*d_read)(struct rfs_device *rdev, int blkno);
};
```
**作用**：模拟块设备，提供读写接口

#### **RFS磁盘布局**
```
块0: 超级块（rfs_superblock）
块1-10: inode区（每块32个inode，共320个）
块11: 数据块位图（标记块使用情况）
块12-127: 数据块区（116个可用数据块）
```

### 2.3 进程文件管理层

#### **proc_file_management**
```c
typedef struct proc_file_management_t {
  struct dentry *cwd;                    // 当前工作目录
  struct file opened_files[MAX_FILES];   // 打开文件数组（128）
  int nfiles;                            // 已打开文件数
} proc_file_management;
```
**作用**：每个进程的文件描述符表

## 三、关键机制

### 3.1 哈希缓存机制

#### **dentry哈希表**
- **键**：`(parent_dentry, name)` 二元组
- **值**：`dentry` 指针
- **哈希函数**：DJB2算法 + 父指针
- **作用**：加速路径查找，避免重复解析

#### **vinode哈希表**
- **键**：`(super_block, inum)` 二元组
- **值**：`vinode` 指针
- **哈希函数**：`inum % HASH_TABLE_SIZE`
- **作用**：
  - 避免重复读取磁盘inode
  - 处理硬链接时共享同一vinode对象

#### **缓存插入时机**
```c
// 创建新文件时
hash_put_dentry(file_dentry);
hash_put_vinode(new_inode);

// 路径查找时未命中哈希表
struct vinode *found_vinode = viop_lookup(parent->dentry_inode, this);
hash_put_vinode(found_vinode);
hash_put_dentry(this);
```

### 3.2 引用计数机制

#### **dentry引用计数（d_ref）**
- 表示有多少个打开的 `file` 结构引用该dentry
- 当 `d_ref=0` 且文件关闭时，可以释放dentry

#### **vinode引用计数（ref）**
- 表示有多少个dentry指向该vinode
- 当 `ref=0` 且 `nlinks=0` 时，inode已从磁盘删除，释放vinode

#### **硬链接计数（nlinks）**
- 表示磁盘上有多少个目录项指向该inode
- `nlinks=0` 时文件从磁盘删除

### 3.3 路径解析机制

#### **lookup_final_dentry**
```c
struct dentry *lookup_final_dentry(const char *path, 
                                   struct dentry **parent,
                                   char *miss_name)
```

**流程**：
1. 使用 `strtok` 按 `/` 分割路径
2. 对每一级目录：
   - 先查哈希表 `hash_get_dentry(parent, token)`
   - 未命中则调用 `viop_lookup` 查磁盘
   - 检查vinode哈希表避免重复创建（处理硬链接）
   - 插入哈希表缓存
3. 返回最终dentry或NULL（记录缺失名称）

### 3.4 文件操作流程

#### **打开文件（vfs_open）**
```
1. lookup_final_dentry 查找路径
2. 未找到 + O_CREAT → 调用 viop_create 创建
3. 分配 file 结构
4. 调用 viop_hook_open（HostFS需要真实打开）
5. 返回 file 指针
```

#### **关闭文件（vfs_close）**
```
1. 调用 viop_hook_close（HostFS需要真实关闭）
2. dentry->d_ref--
3. d_ref=0 时：
   - 从哈希表删除dentry
   - vinode->ref--
   - ref=0 时写回磁盘并释放vinode
```

#### **删除文件（vfs_unlink）**
```
1. 检查 d_ref>0（文件仍打开）
2. 调用 viop_unlink 修改磁盘
3. 从哈希表删除dentry
4. vinode->ref--
5. nlinks=0 且 ref=0 时彻底释放vinode
```

### 3.5 直接块索引

- 每个文件只支持 **10个直接块**
- 块大小 4KB，单文件最大 **40KB**
- `addrs[]` 数组存储块号
- 读写时根据偏移量计算：`block_id = offset / PGSIZE`

## 四、支持的文件系统

### 4.1 RFS（RAMDISK File System）
- 运行在内存模拟的磁盘上
- 支持完整的创建/删除/硬链接操作
- 使用位图管理空闲块
- 目录操作需要缓存（`rfs_dir_cache`）

### 4.2 HostFS
- 通过 spike 接口访问宿主机文件系统
- 映射到 `./hostfs_root` 目录
- inode编号使用宿主文件描述符
- 不支持硬链接（返回错误）

## 五、设计特点

### 优点
1. **分层清晰**：VFS抽象层解耦上层应用和底层文件系统
2. **多态操作**：通过 `vinode_ops` 支持多种文件系统
3. **高效缓存**：哈希表避免重复磁盘访问
4. **硬链接支持**：通过vinode共享实现

### 限制
1. **简单索引**：只有直接块，文件大小限制40KB
2. **固定大小**：RAMDISK固定128块（512KB）
3. **无间接块**：不支持大文件
4. **无权限控制**：没有用户/组/权限位
5. **同步IO**：所有操作阻塞式完成

## 六、关键函数总结

| 函数 | 作用 |
|------|------|
| `vfs_init` | 初始化哈希表 |
| `vfs_mount` | 挂载文件系统到根或子目录 |
| `vfs_open` | 打开/创建文件 |
| `vfs_read/write` | 读写文件（调用viop_*） |
| `lookup_final_dentry` | 路径解析核心函数 |
| `hash_put_dentry/vinode` | 插入缓存 |
| `hash_get_dentry/vinode` | 查询缓存 |
| `viop_create` | 文件系统特定创建操作 |
| `viop_lookup` | 文件系统特定查找操作 |

---

**总结**：该实现是一个教学级VFS，麻雀虽小五脏俱全，展示了现代操作系统文件系统的核心概念：抽象层、缓存、引用计数、路径解析、多态操作接口。
