/*
 * routines that scan and load a (host) Executable and Linkable Format (ELF) file
 * into the (emulated) memory.
 */

#include "elf.h"
#include "string.h"
#include "riscv.h"
#include "spike_interface/spike_utils.h"

//
// the implementation of allocater. allocates memory space for later segment loading
//
static void *elf_alloc_mb(elf_ctx *ctx, uint64 elf_pa, uint64 elf_va, uint64 size) {
  // directly returns the virtual address as we are in the Bare mode in lab1_x
  return (void *)elf_va;
}

//
// actual file reading, using the spike file interface.
//
static uint64 elf_fpread(elf_ctx *ctx, void *dest, uint64 nb, uint64 offset) {
  elf_info *msg = (elf_info *)ctx->info;
  // call spike file utility to load the content of elf file into memory.
  // spike_file_pread will read the elf file (msg->f) from offset to memory (indicated by
  // *dest) for nb bytes.
  return spike_file_pread(msg->f, dest, nb, offset);
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

  return EL_OK;
}

//
// load the elf segments to memory regions as we are in Bare mode in lab1
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
  }

  return EL_OK;
}



//
// returns the number (should be 1) of string(s) after PKE kernel in command line.
// and store the string(s) in arg_bug_msg.
//
size_t parse_args(arg_buf *arg_bug_msg) {
  // HTIFSYS_getmainvars frontend call reads command arguments to (input) *arg_bug_msg
  long r = frontend_syscall(HTIFSYS_getmainvars, (uint64)arg_bug_msg,
      sizeof(*arg_bug_msg), 0, 0, 0, 0, 0);
  kassert(r == 0);

  size_t pk_argc = arg_bug_msg->buf[0];
  uint64 *pk_argv = &arg_bug_msg->buf[1];

  int arg = 1;  // skip the PKE OS kernel string, leave behind only the application name
  for (size_t i = 0; arg + i < pk_argc; i++)
    arg_bug_msg->argv[i] = (char *)(uintptr_t)pk_argv[arg + i];

  //returns the number of strings after PKE kernel in command line
  return pk_argc - arg;
}

//
// load the elf of user application, by using the spike file interface.
//
void load_bincode_from_host_elf(process *p) {
  arg_buf arg_bug_msg;

  // retrieve command line arguements
  size_t argc = parse_args(&arg_bug_msg);
  if (!argc) panic("You need to specify the application program!\n");

  sprint("Application: %s\n", arg_bug_msg.argv[0]);

  //elf loading. elf_ctx is defined in kernel/elf.h, used to track the loading process.
  elf_ctx elfloader;
  // elf_info is defined above, used to tie the elf file and its corresponding process.
  elf_info info;

  info.f = spike_file_open(arg_bug_msg.argv[0], O_RDONLY, 0);
  info.p = p;
  // IS_ERR_VALUE is a macro defined in spike_interface/spike_htif.h
  if (IS_ERR_VALUE(info.f)) panic("Fail on openning the input application program.\n");

  // init elfloader context. elf_init() is defined above.
  if (elf_init(&elfloader, &info) != EL_OK)
    panic("fail to init elfloader.\n");

  // load elf. elf_load() is defined above.
  if (elf_load(&elfloader) != EL_OK) panic("Fail on loading elf.\n");

  // entry (virtual, also physical in lab1_x) address
  p->trapframe->epc = elfloader.ehdr.entry;

  // close the host spike file
  spike_file_close( info.f );

  sprint("Application program entry point (virtual address): 0x%lx\n", p->trapframe->epc);
}

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