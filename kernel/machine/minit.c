/*
 * Machine-mode C startup codes
 */

#include "util/types.h"
#include "kernel/riscv.h"
#include "kernel/config.h"
#include "spike_interface/spike_utils.h"
#include "kernel/sync_utils.h"

//
// global variables are placed in the .data section.
// stack0 is the privilege mode stack(s) of the proxy kernel on CPU(s)
// allocates 4KB stack space for each processor (hart)

__attribute__((aligned(16))) char stack0[4096 * NCPU];

// barrier used to ensure that spike/HTIF/device-tree initialization is executed only
// once by hart0, while the other harts wait until the unique resources are ready.
// Without this barrier, hart1 could access uninitialized HTIF state and crash.
static volatile int g_init_barrier = 0;

// sstart() is the supervisor state entry point defined in kernel/kernel.c
extern void s_start();
// M-mode trap entry point, added @lab1_2
extern void mtrapvec();

// htif is defined in spike_interface/spike_htif.c, marks the availability of HTIF
extern uint64 htif;
// g_mem_size is defined in spike_interface/spike_memory.c, size of the emulated memory
extern uint64 g_mem_size;

// struct riscv_regs is define in kernel/riscv.h. In multicore we need one M-mode
// interrupt frame per hart to avoid concurrent M-mode traps clobbering each other's
// saved registers. indexed by hartid.
riscv_regs g_itrframe[NCPU];

//
// get the information of HTIF (calling interface) and the emulated memory by
// parsing the Device Tree Blog (DTB, actually DTS) stored in memory.
//
// the role of DTB is similar to that of Device Address Resolution Table (DART)
// in Intel series CPUs. it records the details of devices and memory of the
// platform simulated using Spike.
//
void init_dtb(uint64 dtb) {
  // defined in spike_interface/spike_htif.c, enabling Host-Target InterFace (HTIF)
  query_htif(dtb);
  if (htif) sprint("HTIF is available!\r\n");

  // defined in spike_interface/spike_memory.c, obtain information about emulated memory
  query_mem(dtb);
  sprint("(Emulated) memory size: %ld MB\n", g_mem_size >> 20);
}

//
// delegate (almost all) interrupts and most exceptions to S-mode.
// after delegation, syscalls will handled by the PKE OS kernel running in S-mode.
//
static void delegate_traps() {
  // supports_extension macro is defined in kernel/riscv.h
  if (!supports_extension('S')) {
    // confirm that our processor supports supervisor mode. abort if it does not.
    sprint("S mode is not supported.\n");
    return;
  }

  // macros used in following two statements are defined in kernel/riscv.h
  uintptr_t interrupts = MIP_SSIP | MIP_STIP | MIP_SEIP;
  uintptr_t exceptions = (1U << CAUSE_MISALIGNED_FETCH) | (1U << CAUSE_FETCH_PAGE_FAULT) |
                         (1U << CAUSE_BREAKPOINT) | (1U << CAUSE_LOAD_PAGE_FAULT) |
                         (1U << CAUSE_STORE_PAGE_FAULT) | (1U << CAUSE_USER_ECALL);

  // writes 64-bit values (interrupts and exceptions) to 'mideleg' and 'medeleg' (two
  // priviledged registers of RV64G machine) respectively.
  //
  // write_csr and read_csr are macros defined in kernel/riscv.h
  write_csr(mideleg, interrupts);
  write_csr(medeleg, exceptions);
  assert(read_csr(mideleg) == interrupts);
  assert(read_csr(medeleg) == exceptions);
}

//
// enabling timer interrupt (irq) in Machine mode. added @lab1_3
//
void timerinit(uintptr_t hartid) {
  // fire timer irq after TIMER_INTERVAL from now.
  *(uint64*)CLINT_MTIMECMP(hartid) = *(uint64*)CLINT_MTIME + TIMER_INTERVAL;

  // enable machine-mode timer irq in MIE (Machine Interrupt Enable) csr.
  write_csr(mie, read_csr(mie) | MIE_MTIE);
}

//
// m_start: machine mode C entry point.
//
void m_start(uintptr_t hartid, uintptr_t dtb) {
  // Record hartid into tp so that after we drop to S-mode (where reading mhartid is
  // illegal), the kernel can still know which hart it is running on.
  write_tp(hartid);

  // For multicore, only hart0 should initialize unique simulator resources (HTIF, file
  // interfaces, DTB parsing). Other harts must wait until initialization completes to
  // avoid racing on the one-copy hardware resources.
  if (hartid == 0) {
    // init the spike file interface (stdin,stdout,stderr)
    // functions with "spike_" prefix are all defined in codes under spike_interface/,
    // sprint is also defined in spike_interface/spike_utils.c
    spike_file_init();

    // init HTIF (Host-Target InterFace) and memory by using the Device Table Blob (DTB)
    // init_dtb() is defined above.
    init_dtb(dtb);
  }

  sprint("In m_start, hartid:%d\n", hartid);

  // Synchronize all harts here: hart0 finishes the unique initialization first, then
  // other harts continue. This prevents secondary harts from touching HTIF/memory info
  // before it is ready.
  sync_barrier(&g_init_barrier, NCPU);

  // save the address of trap frame for interrupt in M mode to "mscratch".
  // mscratch 是 M 态陷入时的临时寄存器存放区指针；填入按 hart 划分的 g_itrframe
  // 可让每个 hart 在陷入时有独立的保存区，避免多核同时陷入时互相覆盖寄存器。
  write_csr(mscratch, &g_itrframe[hartid]);

  // set previous privilege mode to S (Supervisor), and will enter S mode after 'mret'
  // mstatus 的 MPP 字段记录 mret 返回时要进入的目标特权级；清零后置为 S
  // 等于告诉硬件：mret 应当跳转到 S 态，从而完成从引导的 M 态降级。
  write_csr(mstatus, ((read_csr(mstatus) & ~MSTATUS_MPP_MASK) | MSTATUS_MPP_S));

  // set M Exception Program Counter to sstart, for mret (requires gcc -mcmodel=medany)
  // mepc 保存 mret 返回的指令地址；设为 s_start 就是指定返回后执行的入口，
  // 因此 mret 会将 PC 载入此值，从而跳入 S 态内核入口。
  write_csr(mepc, (uint64)s_start);

  // setup trap handling vector for machine mode. added @lab1_2
  // mtvec 决定 M 态异常/中断时跳转的入口；设为 mtrapvec 后，后续任何 M 态陷入
  // 都会跳到该向量，确保有统一的 M 态陷入处理流程。
  write_csr(mtvec, (uint64)mtrapvec);

  // enable machine-mode interrupts. added @lab1_3
  // mstatus.MIE 是 M 态全局中断使能位；置 1 后配合 mie/sie 的具体使能，
  // 才会真正允许 M 态响应中断（如计时器触发）。
  write_csr(mstatus, read_csr(mstatus) | MSTATUS_MIE);

  // delegate all interrupts and exceptions to supervisor mode.
  // 委派后，硬件在触发这些中断/异常时直接转交给 S 态处理，M 态无需介入，
  // 这样内核的大部分逻辑运行在 S 态，符合分层特权模型。具体位由 delegate_traps 设置。
  delegate_traps();

  // also enables interrupt handling in supervisor mode. added @lab1_3
  // sie 是 S 态中断使能寄存器；打开 SEIE/STIE/SSIE 位后，配合先前的委派，
  // S 态即可响应外部/定时器/软件中断，实现常规 OS 中断路径。
  write_csr(sie, read_csr(sie) | SIE_SEIE | SIE_STIE | SIE_SSIE);

  // init timing. added @lab1_3
  // timerinit 为当前 hart 设置 CLINT 的 mtimecmp，使其在未来某时间触发 MTI 中断；
  // 结合上面的中断使能，可周期性获得时钟中断用于调度等功能。
  timerinit(hartid);

  // switch to supervisor mode (S mode) and jump to s_start(), i.e., set pc to mepc
  // mret 会按 mstatus.MPP 切换到 S 态，并将 PC 置为 mepc；因此在完成寄存器配置、
  // 委派与中断使能后执行 mret，正式进入 S 态内核执行流。
  asm volatile("mret");
}
