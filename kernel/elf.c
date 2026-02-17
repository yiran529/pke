/*
 * routines that scan and load a (host) Executable and Linkable Format (ELF) file
 * into the (emulated) memory.
 */

#include "elf.h"
#include "util/string.h"
#include "riscv.h"
#include "vmm.h"
#include "pmm.h"
#include "vfs.h"
#include "spike_interface/spike_utils.h"

// hard cap for directory/file entries we cache from DWARF line table
#define DEBUG_DIR_CAP 64
#define DEBUG_FILE_CAP 64
// round up value to the next multiple of align (align must be power-of-two)
#define DALIGN_UP(val, align) (((val) + ((align) - 1)) & ~((align) - 1))

// forward declarations
static void load_debug_line_section(elf_ctx *ctx,
    uint64 (*reader)(elf_ctx *, void *, uint64, uint64));

//
// the implementation of allocater. allocates memory space for later segment loading.
// this allocater is heavily modified @lab2_1, where we do NOT work in bare mode.
//
static void *elf_alloc_mb(elf_ctx *ctx, uint64 elf_pa, uint64 elf_va, uint64 size) {
  elf_info *msg = (elf_info *)ctx->info;
  // we assume that size of proram segment is smaller than a page.
  kassert(size < PGSIZE);
  void *pa = alloc_page();
  if (pa == 0) panic("uvmalloc mem alloc falied\n");

  memset((void *)pa, 0, PGSIZE);
  user_vm_map((pagetable_t)msg->p->pagetable, elf_va, PGSIZE, (uint64)pa,
         prot_to_type(PROT_WRITE | PROT_READ | PROT_EXEC, 1));

  return pa;
}

//
// actual file reading, using the vfs file interface.
//
static uint64 elf_fpread(elf_ctx *ctx, void *dest, uint64 nb, uint64 offset) {
  elf_info *msg = (elf_info *)ctx->info;
  vfs_lseek(msg->f, offset, SEEK_SET);
  return vfs_read(msg->f, dest, nb);
}

//
// init elf_ctx, a data structure that loads the elf.
//
elf_status elf_init(elf_ctx *ctx, void *info) {
  ctx->info = info;

  // load the elf header
  if (elf_fpread(ctx, &ctx->ehdr, sizeof(ctx->ehdr), 0) != sizeof(ctx->ehdr)) return EL_EIO;

  // check the signature (magic value) of the elf
  if (ctx->ehdr.magic != ELF_MAGIC) return EL_NOTELF;
  sprint("[DEBUG] EL_OK\n");
  return EL_OK;
}

///////////////////////////////////
// added @lab1_challenge2
// leb128 (little-endian base 128) is a variable-length
// compression algoritm in DWARF
void read_uleb128(uint64 *out, char **off) {
    uint64 value = 0; int shift = 0; uint8 b;
    for (;;) {
        b = *(uint8 *)(*off); (*off)++;
        value |= ((uint64)b & 0x7F) << shift;
        shift += 7;
        if ((b & 0x80) == 0) break;
    }
    if (out) *out = value;
}
void read_sleb128(int64 *out, char **off) {
    int64 value = 0; int shift = 0; uint8 b;
    for (;;) {
        b = *(uint8 *)(*off); (*off)++;
        value |= ((uint64_t)b & 0x7F) << shift;
        shift += 7;
        if ((b & 0x80) == 0) break;
    }
    if (shift < 64 && (b & 0x40)) value |= -(1 << shift);
    if (out) *out = value;
}
// Since reading below types through pointer cast requires aligned address,
// so we can only read them byte by byte
void read_uint64(uint64 *out, char **off) {
    *out = 0;
    for (int i = 0; i < 8; i++) {
        *out |= (uint64)(**off) << (i << 3); (*off)++;
    }
}
void read_uint32(uint32 *out, char **off) {
    *out = 0;
    for (int i = 0; i < 4; i++) {
        *out |= (uint32)(**off) << (i << 3); (*off)++;
    }
}
void read_uint16(uint16 *out, char **off) {
    *out = 0;
    for (int i = 0; i < 2; i++) {
        *out |= (uint16)(**off) << (i << 3); (*off)++;
    }
}

/*
* analyzis the data in the debug_line section
*
* the function needs 3 parameters: elf context, data in the debug_line section
* and length of debug_line section
*
* make 3 arrays:
* "process->dir" stores all directory paths of code files
* "process->file" stores all code file names of code files and their directory path index of array "dir"
* "process->line" stores all relationships map instruction addresses to code line numbers
* and their code file name index of array "file"
*/
void make_addr_line(elf_ctx *ctx, char *debug_line, uint64 length) {
   process *p = ((elf_info *)ctx->info)->p;
    p->debugline = debug_line;
    uint64 meta_base = DALIGN_UP((uint64)debug_line + length, 8);
    // directory name char pointer array (limited to DEBUG_DIR_CAP entries)
    p->dir = (char **)meta_base; int dir_ind = 0, dir_base;
    // file name char pointer array (each entry carries back-pointer dir index)
    p->file = (code_file *)(p->dir + DEBUG_DIR_CAP); int file_ind = 0, file_base;
    // table array storing pc->(line,file) mapping generated from DWARF opcodes
    p->line = (addr_line *)(p->file + DEBUG_FILE_CAP); p->line_ind = 0;
    char *off = debug_line;
    while (off < debug_line + length) { // iterate each compilation unit(CU)
        debug_header *dh = (debug_header *)off; off += sizeof(debug_header);
        dir_base = dir_ind; file_base = file_ind;
        // get directory name char pointer in this CU
        while (*off != 0) {
            p->dir[dir_ind++] = off; while (*off != 0) off++; off++;
        }
        off++;
        // get file name char pointer in this CU
        while (*off != 0) {
            p->file[file_ind].file = off; while (*off != 0) off++; off++;
            uint64 dir; read_uleb128(&dir, &off);
            p->file[file_ind++].dir = dir - 1 + dir_base;
            read_uleb128(NULL, &off); read_uleb128(NULL, &off);
        }
        off++; addr_line regs; regs.addr = 0; regs.file = 1; regs.line = 1;
        // simulate the state machine op code
        for (;;) {
            uint8 op = *(off++);
            switch (op) {
                case 0: // Extended Opcodes
                    read_uleb128(NULL, &off); op = *(off++);
                    switch (op) {
                        case 1: // DW_LNE_end_sequence
                            if (p->line_ind > 0 && p->line[p->line_ind - 1].addr == regs.addr) p->line_ind--;
                            p->line[p->line_ind] = regs; p->line[p->line_ind].file += file_base - 1;
                            p->line_ind++; goto endop;
                        case 2: // DW_LNE_set_address
                            read_uint64(&regs.addr, &off); break;
                        // ignore DW_LNE_define_file
                        case 4: // DW_LNE_set_discriminator
                            read_uleb128(NULL, &off); break;
                    }
                    break;
                case 1: // DW_LNS_copy
                    if (p->line_ind > 0 && p->line[p->line_ind - 1].addr == regs.addr) p->line_ind--;
                    p->line[p->line_ind] = regs; p->line[p->line_ind].file += file_base - 1;
                    p->line_ind++; break;
                case 2: { // DW_LNS_advance_pc
                            uint64 delta; read_uleb128(&delta, &off);
                            regs.addr += delta * dh->min_instruction_length;
                            break;
                        }
                case 3: { // DW_LNS_advance_line
                            int64 delta; read_sleb128(&delta, &off);
                            regs.line += delta; break; } case 4: // DW_LNS_set_file
                        read_uleb128(&regs.file, &off); break;
                case 5: // DW_LNS_set_column
                        read_uleb128(NULL, &off); break;
                case 6: // DW_LNS_negate_stmt
                case 7: // DW_LNS_set_basic_block
                        break;
                case 8: { // DW_LNS_const_add_pc
                            int adjust = 255 - dh->opcode_base;
                            int delta = (adjust / dh->line_range) * dh->min_instruction_length;
                            regs.addr += delta; break;
                        }
                case 9: { // DW_LNS_fixed_advanced_pc
                            uint16 delta; read_uint16(&delta, &off);
                            regs.addr += delta;
                            break;
                        }
                        // ignore 10, 11 and 12
                default: { // Special Opcodes
                             int adjust = op - dh->opcode_base;
                             int addr_delta = (adjust / dh->line_range) * dh->min_instruction_length;
                             int line_delta = dh->line_base + (adjust % dh->line_range);
                             regs.addr += addr_delta;
                             regs.line += line_delta;
                             if (p->line_ind > 0 && p->line[p->line_ind - 1].addr == regs.addr) p->line_ind--;
                             p->line[p->line_ind] = regs; p->line[p->line_ind].file += file_base - 1;
                             p->line_ind++; break;
                         }
            }
        }
endop:;
    }
    // for (int i = 0; i < p->line_ind; i++)
    //     sprint("%p %d %d\n", p->line[i].addr, p->line[i].line, p->line[i].file);
}
// compute trailing space for dir/file/line arrays behind raw .debug_line
static uint64 calc_debug_aux_size(uint64 debug_len) {
    uint64 dir_bytes = DEBUG_DIR_CAP * sizeof(char *);
    uint64 file_bytes = DEBUG_FILE_CAP * sizeof(code_file);
    uint64 line_bytes = debug_len * sizeof(addr_line);
    return DALIGN_UP(dir_bytes + file_bytes + line_bytes, 8);
}

// parse section headers, allocate buffer via kmalloc, and build lookup tables.
// 'reader' is the file-read function (elf_fpread or elf_fpread_vfs).
static void load_debug_line_section(elf_ctx *ctx,
    uint64 (*reader)(elf_ctx *, void *, uint64, uint64)) {
    if (ctx->ehdr.shoff == 0 || ctx->ehdr.shnum == 0) return;
    if (ctx->ehdr.shstrndx == 0 || ctx->ehdr.shstrndx >= ctx->ehdr.shnum) return;

    // read section header string table header
    elf_sect_header shstr;
    uint64 shstr_off = ctx->ehdr.shoff + (uint64)ctx->ehdr.shstrndx * ctx->ehdr.shentsize;
    if (reader(ctx, &shstr, sizeof(shstr), shstr_off) != sizeof(shstr)) return;
    if (shstr.size == 0 || shstr.size > PGSIZE) return;  // sanity: shstrtab should be small

    // read section header string table contents
    char *shstrtab_buf = (char *)kmalloc(shstr.size);
    if (reader(ctx, shstrtab_buf, shstr.size, shstr.offset) != shstr.size) return;

    // iterate all section headers looking for .debug_line
    for (uint16 i = 0; i < ctx->ehdr.shnum; ++i) {
        elf_sect_header shdr;
        uint64 off = ctx->ehdr.shoff + (uint64)i * ctx->ehdr.shentsize;
        if (reader(ctx, &shdr, sizeof(shdr), off) != sizeof(shdr)) return;
        if (shdr.name >= shstr.size) continue;

        const char *name = shstrtab_buf + shdr.name;
        if (strcmp(name, ".debug_line") != 0) continue;

        // allocate debug_line data + metadata arrays in one contiguous block
        uint64 prefix = DALIGN_UP(shdr.size, 8);
        uint64 aux = calc_debug_aux_size(shdr.size);
        uint64 total = prefix + aux;
        char *debug_buf = (char *)kmalloc(total);
        memset(debug_buf, 0, total);
        if (reader(ctx, debug_buf, shdr.size, shdr.offset) != shdr.size) return;
        make_addr_line(ctx, debug_buf, shdr.size);
        return;
    }
}

///////////////////////////////////
//
// load the elf segments to memory regions.
//
elf_status elf_load(elf_ctx *ctx) {
  // elf_prog_header structure is defined in kernel/elf.h
  elf_prog_header ph_addr;
  int i, off;

  // traverse the elf program segment headers
  for (i = 0, off = ctx->ehdr.phoff; i < ctx->ehdr.phnum; i++, off += sizeof(ph_addr)) {
    // read segment headers
    if (elf_fpread(ctx, (void *)&ph_addr, sizeof(ph_addr), off) != sizeof(ph_addr)) return EL_EIO;

    if (ph_addr.type != ELF_PROG_LOAD) continue;
    if (ph_addr.memsz < ph_addr.filesz) return EL_ERR;
    if (ph_addr.vaddr + ph_addr.memsz < ph_addr.vaddr) return EL_ERR;

    // allocate memory block before elf loading
    void *dest = elf_alloc_mb(ctx, ph_addr.vaddr, ph_addr.vaddr, ph_addr.memsz);

    // actual loading
    if (elf_fpread(ctx, dest, ph_addr.memsz, ph_addr.off) != ph_addr.memsz)
      return EL_EIO;

    // record the vm region in proc->mapped_info. added @lab3_1
    int j;
    for( j=0; j<PGSIZE/sizeof(mapped_region); j++ ) //seek the last mapped region
      if( (process*)(((elf_info*)(ctx->info))->p)->mapped_info[j].va == 0x0 ) break;

    ((process*)(((elf_info*)(ctx->info))->p))->mapped_info[j].va = ph_addr.vaddr;
    ((process*)(((elf_info*)(ctx->info))->p))->mapped_info[j].npages = 1;

    // SEGMENT_READABLE, SEGMENT_EXECUTABLE, SEGMENT_WRITABLE are defined in kernel/elf.h
    if( ph_addr.flags == (SEGMENT_READABLE|SEGMENT_EXECUTABLE) ){
      ((process*)(((elf_info*)(ctx->info))->p))->mapped_info[j].seg_type = CODE_SEGMENT;
      sprint( "CODE_SEGMENT added at mapped info offset:%d\n", j );
    }else if ( ph_addr.flags == (SEGMENT_READABLE|SEGMENT_WRITABLE) ){
      ((process*)(((elf_info*)(ctx->info))->p))->mapped_info[j].seg_type = DATA_SEGMENT;
      sprint( "DATA_SEGMENT added at mapped info offset:%d\n", j );
    }else
      panic( "unknown program segment encountered, segment flag:%d.\n", ph_addr.flags );

    ((process*)(((elf_info*)(ctx->info))->p))->total_mapped_region ++;
  }

  return EL_OK;
}

//
// load the elf of user application, by using the spike file interface.
//
void load_bincode_from_host_elf(process *p, char *filename) {
  sprint("Application: %s\n", filename);

  //elf loading. elf_ctx is defined in kernel/elf.h, used to track the loading process.
  elf_ctx elfloader;
  // elf_info is defined above, used to tie the elf file and its corresponding process.
  elf_info info;
  sprint("filename: %s\n", filename);
  info.f = vfs_open(filename, O_RDONLY);
  info.p = p;
  // IS_ERR_VALUE is a macro defined in spike_interface/spike_htif.h
  if (IS_ERR_VALUE(info.f)) panic("Fail on openning the input application program.\n");

  // init elfloader context. elf_init() is defined above.
  if (elf_init(&elfloader, &info) != EL_OK)
    panic("fail to init elfloader.\n");

  // load elf. elf_load() is defined above.
  if (elf_load(&elfloader) != EL_OK) panic("Fail on loading elf.\n");

  // save heap mark before debug info allocation (so it can be reclaimed on process exit)
  p->debug_heap_mark = kmalloc_mark();
  // load DWARF .debug_line section for backtrace line-number info
  load_debug_line_section(&elfloader, elf_fpread);

  // entry (virtual, also physical in lab1_x) address
  p->trapframe->epc = elfloader.ehdr.entry;

  // record executable path for backtrace
  safestrcpy(p->exe_path, filename, sizeof(p->exe_path));

  // close the vfs file
  vfs_close( info.f );

  sprint("Application program entry point (virtual address): 0x%lx\n", p->trapframe->epc);
}

/* Below are helper funcion for implmentation of exec.
  Basically, they are 'exec verion' of the funcitons above. */

//
// ELF 文件读取所需的信息结构（使用 VFS）
//
typedef struct elf_vfs_info_t {
  struct file *f;       // VFS 文件结构
  process *p;           // 进程指针
} elf_vfs_info;

//
// VFS版本的elf_alloc_mb函数 
//
static void *elf_alloc_mb_vfs(elf_ctx *ctx, uint64 elf_pa, uint64 elf_va, uint64 size) {
  elf_vfs_info *msg = (elf_vfs_info *)ctx->info;
  // we assume that size of proram segment is smaller than a page.
  kassert(size < PGSIZE);
  void *pa = alloc_page();
  if (pa == 0) panic("uvmalloc mem alloc falied\n");

  memset((void *)pa, 0, PGSIZE);
  user_vm_map((pagetable_t)msg->p->pagetable, elf_va, PGSIZE, (uint64)pa,
         prot_to_type(PROT_WRITE | PROT_READ | PROT_EXEC, 1));

  return pa;
}

//
// VFS 版本的文件读取函数（替代 spike_file_pread）
//
static uint64 elf_fpread_vfs(elf_ctx *ctx, void *dest, uint64 nb, uint64 offset) {
  elf_vfs_info *msg = (elf_vfs_info *)ctx->info;
  
  // 使用 VFS 的 lseek 定位到指定偏移
  vfs_lseek(msg->f, offset, SEEK_SET);
  
  // 通过 VFS 读取数据
  return vfs_read(msg->f, (char*)dest, nb);
}

//
// VFS 版本的 ELF 初始化（替代 elf_init）
//
elf_status elf_init_vfs(elf_ctx *ctx, void *info) {
  ctx->info = info;

  // 从文件开头读取 ELF header
  if (elf_fpread_vfs(ctx, &ctx->ehdr, sizeof(ctx->ehdr), 0) != sizeof(ctx->ehdr))
    return EL_EIO;

  // 检查 ELF 魔数
  if (ctx->ehdr.magic != ELF_MAGIC)
    return EL_NOTELF;

  return EL_OK;
}

//
// load the elf segments to memory regions.
//
elf_status elf_load_vfs(elf_ctx *ctx) {
  // elf_prog_header structure is defined in kernel/elf.h
  elf_prog_header ph_addr;
  int i, off;

  // traverse the elf program segment headers
  for (i = 0, off = ctx->ehdr.phoff; i < ctx->ehdr.phnum; i++, off += sizeof(ph_addr)) {
    // read segment headers
    if (elf_fpread_vfs(ctx, (void *)&ph_addr, sizeof(ph_addr), off) != sizeof(ph_addr)) return EL_EIO;

    if (ph_addr.type != ELF_PROG_LOAD) continue;
    if (ph_addr.memsz < ph_addr.filesz) return EL_ERR;
    if (ph_addr.vaddr + ph_addr.memsz < ph_addr.vaddr) return EL_ERR;

    // allocate memory block before elf loading
    void *dest = elf_alloc_mb_vfs(ctx, ph_addr.vaddr, ph_addr.vaddr, ph_addr.memsz);

    // actual loading
    if (elf_fpread_vfs(ctx, dest, ph_addr.memsz, ph_addr.off) != ph_addr.memsz)
      return EL_EIO;

    // record the vm region in proc->mapped_info. added @lab3_1
    int j;
    for( j=0; j<PGSIZE/sizeof(mapped_region); j++ ) //seek the last mapped region
      if( (process*)(((elf_vfs_info*)(ctx->info))->p)->mapped_info[j].va == 0x0 ) break;

    ((process*)(((elf_vfs_info*)(ctx->info))->p))->mapped_info[j].va = ph_addr.vaddr;
    ((process*)(((elf_vfs_info*)(ctx->info))->p))->mapped_info[j].npages = 1;

    // SEGMENT_READABLE, SEGMENT_EXECUTABLE, SEGMENT_WRITABLE are defined in kernel/elf.h
    if( ph_addr.flags == (SEGMENT_READABLE|SEGMENT_EXECUTABLE) ){
      ((process*)(((elf_vfs_info*)(ctx->info))->p))->mapped_info[j].seg_type = CODE_SEGMENT;
      sprint( "CODE_SEGMENT added at mapped info offset:%d\n", j );
    }else if ( ph_addr.flags == (SEGMENT_READABLE|SEGMENT_WRITABLE) ){
      ((process*)(((elf_vfs_info*)(ctx->info))->p))->mapped_info[j].seg_type = DATA_SEGMENT;
      sprint( "DATA_SEGMENT added at mapped info offset:%d\n", j );
    }else
      panic( "unknown program segment encountered, segment flag:%d.\n", ph_addr.flags );

    ((process*)(((elf_vfs_info*)(ctx->info))->p))->total_mapped_region ++;
  }

  return EL_OK;
}

//
// load the elf of user application, by using the spike file interface.
//
void load_bincode_from_host_elf_for_exec(process *p, char* pathname) {
  sprint("Application: %s\n", pathname);

  //elf loading. elf_ctx is defined in kernel/elf.h, used to track the loading process.
  elf_ctx elfloader;
  // elf_info is defined above, used to tie the elf file and its corresponding process.
  elf_vfs_info info;

  info.f = vfs_open(pathname, O_RDONLY);
  info.p = p;
  // IS_ERR_VALUE is a macro defined in spike_interface/spike_htif.h
  if (info.f == NULL) panic("Fail on openning the input application program.\n");

  // init elfloader context. elf_init() is defined above.
  if (elf_init_vfs(&elfloader, &info) != EL_OK)
    panic("fail to init elfloader.\n");

  // load elf. elf_load() is defined above.
  if (elf_load_vfs(&elfloader) != EL_OK) panic("Fail on loading elf.\n");

  // save heap mark before debug info allocation (so it can be reclaimed on process exit)
  p->debug_heap_mark = kmalloc_mark();
  // load DWARF .debug_line section for backtrace line-number info
  load_debug_line_section(&elfloader, elf_fpread_vfs);

  // entry (virtual, also physical in lab1_x) address
  p->trapframe->epc = elfloader.ehdr.entry;

  // record executable path for backtrace
  safestrcpy(p->exe_path, pathname, sizeof(p->exe_path));

  // close the host spike file
  vfs_close( info.f );

  sprint("Application program entry point (virtual address): 0x%lx\n", p->trapframe->epc);
}

// added @lab1_challenge1
// ============================= Below are utils for print_backtrace =============================
void find_all_section(elf_ctx* ctx, elf_section_header* section_headers) {
  for (int i = 0; i < ctx->ehdr.shnum; i++) {
    elf_fpread(ctx, 
              (void*)(section_headers + i), 
              sizeof(elf_section_header), 
              ctx->ehdr.shoff + i * ctx->ehdr.shentsize);
  }
}

void find_shstrtab(elf_ctx* ctx, elf_section_header* section_headers, char* shstrtab) {
  // .shstrtab 的索引在 ehdr.shstrndx 中
  elf_section_header *shstrtab_hdr = &section_headers[ctx->ehdr.shstrndx];

  // 读取 .shstrtab 的内容
  elf_fpread(ctx, shstrtab, shstrtab_hdr->sh_size, shstrtab_hdr->sh_offset);
}

void find_section(elf_ctx* ctx, elf_section_header* section_headers, 
                  char* tg_section_name, elf_section_header* hdr) {

  // 获取shstrtab的内容
  // sprint("[DEBUG] Entered find_section to find %s\n", tg_section_name);
  int shstrtab_sz = section_headers[ctx->ehdr.shstrndx].sh_size;
  char shstrtab[shstrtab_sz + 1];
  find_shstrtab(ctx, section_headers, shstrtab);

  for (int i = 0; i < ctx->ehdr.shnum; i++) {
      // 获取节名 (通过 sh_name 在 shstrtab 中的偏移)
      char *section_name = shstrtab + section_headers[i].sh_name;
      
      // 比较节名
      if (strcmp(section_name, tg_section_name) == 0) {
        *hdr = section_headers[i];
        // sprint("[DEBUG] found tg_sction_name: %s\n", tg_section_name);
        return;
      } 
  }
}

int get_name_by_ra(elf_ctx* ctx, elf_section_header* section_headers, uint64 ra) {
  sprint("[DEBUG] Entered get_name_by_ra with ra: 0x%lx\n", ra);
  find_all_section(ctx, section_headers);

  // 读取符号表
  elf_section_header symtab_hdr;
  find_section(ctx, section_headers, ".symtab", &symtab_hdr);
  int symbol_count = symtab_hdr.sh_size / sizeof(elf_symbol);
  elf_symbol symbols[symbol_count];
  elf_fpread(ctx, (void*)&symbols, symtab_hdr.sh_size, symtab_hdr.sh_offset);

  // 读取字符串表
  elf_section_header strtab_hdr;
  find_section(ctx, section_headers, ".strtab", &strtab_hdr);
  char strtab[strtab_hdr.sh_size + 1];
  elf_fpread(ctx, (void*)strtab, strtab_hdr.sh_size, strtab_hdr.sh_offset);
  
  char *function_name = NULL;

  for (int i = 0; i < symbol_count; i++) {
    elf_symbol *sym = &symbols[i];
    
    // 检查符号类型 (st_info的低4位)
    uint8 symbol_type = sym->st_info & 0x0F;
    
    // STT_FUNC = 2 (函数符号)
    if (symbol_type != 2) continue;
    
    // 检查 ra 是否在函数的地址范围内
    // ra 可能是函数中间的地址，或者是调用后的返回地址
    if (ra >= sym->st_value && ra < sym->st_value + sym->st_size) {
      // 找到了！获取函数名
      // sprint("[DEBUG] matched ra: 0x%lx with st_value: 0x%lx, st_size: 0x%lx\n", 
      //         ra, sym->st_value, sym->st_size);
      function_name = strtab + sym->st_name;
      // sprint("[DEBUG] found function_name: %s\n",function_name);
      sprint("%s\n", function_name);
      return 1;
    }
  }
  return 0;
}