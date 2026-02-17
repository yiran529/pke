#ifndef _ELF_H_
#define _ELF_H_

#include "util/types.h"
#include "process.h"
#include "spike_interface/spike_utils.h"

#define MAX_CMDLINE_ARGS 64

// elf header structure
typedef struct elf_header_t {
  uint32 magic;
  uint8 elf[12];
  uint16 type;      /* Object file type */
  uint16 machine;   /* Architecture */
  uint32 version;   /* Object file version */
  uint64 entry;     /* Entry point virtual address */
  uint64 phoff;     /* Program header table file offset */
  uint64 shoff;     /* Section header table file offset */
  uint32 flags;     /* Processor-specific flags */
  uint16 ehsize;    /* ELF header size in bytes */
  uint16 phentsize; /* Program header table entry size */
  uint16 phnum;     /* Program header table entry count */
  uint16 shentsize; /* Section header table entry size */
  uint16 shnum;     /* Section header table entry count */
  uint16 shstrndx;  /* Section header string table index */
} elf_header;

// segment types, attributes of elf_prog_header_t.flags
#define SEGMENT_READABLE   0x4
#define SEGMENT_EXECUTABLE 0x1
#define SEGMENT_WRITABLE   0x2

// Program segment header.
typedef struct elf_prog_header_t {
  uint32 type;   /* Segment type */
  uint32 flags;  /* Segment flags */
  uint64 off;    /* Segment file offset */
  uint64 vaddr;  /* Segment virtual address */
  uint64 paddr;  /* Segment physical address */
  uint64 filesz; /* Segment size in file */
  uint64 memsz;  /* Segment size in memory */
  uint64 align;  /* Segment alignment */
} elf_prog_header;

#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian
#define ELF_PROG_LOAD 1

typedef enum elf_status_t {
  EL_OK = 0,

  EL_EIO,
  EL_ENOMEM,
  EL_NOTELF,
  EL_ERR,

} elf_status;

typedef struct elf_ctx_t {
  void *info;
  elf_header ehdr;
} elf_ctx;

elf_status elf_init(elf_ctx *ctx, void *info);
elf_status elf_load(elf_ctx *ctx);

elf_status elf_init_vfs(elf_ctx *ctx, void *info);

void load_bincode_from_host_elf(process *p, char *filename);
void load_bincode_from_host_elf_for_exec(process *p, char *pathname);


typedef struct elf_info_t {
  struct file *f;
  process *p;
} elf_info;

// added @lab1_challenge1
typedef struct {
  uint32 sh_name;      // 节名在 .shstrtab 中的偏移
  uint32 sh_type;      // 节类型 (2=SYMTAB, 3=STRTAB)
  uint64 sh_flags;     // 节标志
  uint64 sh_addr;      // 节的虚拟地址
  uint64 sh_offset;    // 节在文件中的偏移 ← 重要
  uint64 sh_size;      // 节的大小 ← 重要
  uint32 sh_link;      // 链接信息 (SYMTAB的sh_link指向对应的STRTAB)
  uint32 sh_info;      // 额外信息
  uint64 sh_addralign; // 对齐
  uint64 sh_entsize;   // 如果包含固定大小的条目，这是条目大小
} elf_section_header;

typedef struct {
  uint32 st_name;   // 符号名在 .strtab 中的偏移
  uint8  st_info;   // 符号类型和绑定属性
  uint8  st_other;  // 保留
  uint16 st_shndx;  // 相关节的索引
  uint64 st_value;  // 符号的值 (函数地址) ← 重要
  uint64 st_size;   // 符号的大小 (函数大小) ← 重要
} elf_symbol;

int get_name_by_ra(elf_ctx* ctx, elf_section_header* section_headers, uint64 ra);

#endif
