# OrbitOS architecture

OrbitOS is a single-address-space, uniprocessor, preemptive kernel for
AArch64 and, since the rv64 port, 64-bit RISC-V (see
[RV64 port](#rv64-port) below). The current design is a small monolithic kernel. The plan is to
shrink it toward a microkernel (see the roadmap in the README), so the
subsystem boundaries below are drawn where IPC boundaries are expected to go later.

## Layers

```mermaid
flowchart TB
    subgraph EL0["EL0 (user)"]
        U[user_prog.c<br/>SVC: write, sleep, yield, exit, getpid]
    end
    subgraph EL1["EL1 (kernel)"]
        V[vectors.S<br/>trap frame save/restore] --> T[trap.c<br/>ESR decode, fault isolation]
        T --> SC[syscall.c]
        T --> IRQ[irq_dispatch]
        IRQ --> TMR[generic timer<br/>100 Hz tick]
        IRQ --> UART[PL011 RX ring]
        TMR --> S[scheduler<br/>8-level priority RR]
        S --> SW[switch.S<br/>cpu_switch]
        SH[shell thread] --> S
        SH --> MM
        S --> SY[semaphore / mutex]
        MM[mm: buddy + kheap + MMU]
        PL[platform.c<br/>FDT probe] --> MM
    end
    subgraph HW["Hardware / firmware"]
        GIC[GIC-400 / GICv2]
        PSCI[PSCI firmware<br/>TF-A on Pi 5, QEMU built-in]
        DTB[(DTB from firmware)]
    end
    U -- svc --> V
    GIC --> V
    SH -- reboot/poweroff --> PSCI
    DTB --> PL
```

| Directory | Contents | Host-tested |
|-----------|----------|-------------|
| `kernel/arch/aarch64/` | `boot.S` (entry, relocation, EL2→EL1), `vectors.S`, `switch.S`, linker script, `trap.c`, `mmu.c`, `cpu.c` (hal.h hooks), `include/arch.h` + `context.h` | no (QEMU/Unicorn) |
| `kernel/arch/riscv64/` | `boot.S` (M-mode entry + SBI trap vector), `sbi.c` (SBI shim), `entry.S` (stvec), `switch.S`, linker script, `trap.c`, `mmu.c` (Sv39), `cpu.c`, `include/` | no (ISS/QEMU) |
| `kernel/core/` | `kernel_main`, console/panic, tick clock, syscalls + user tasks, demos, platform probe | no |
| `kernel/drivers/` | AArch64: PL011, GICv2, generic timer, PSCI. RV64: NS16550, PLIC, SBI timer. Shared UART RX ring (`serial.c`) | no |
| `kernel/mm/` | `buddy.c`, `kheap.c` (portable), `mm.c` glue | buddy, kheap |
| `kernel/sched/` | `runqueue.c` (portable), `sched.c`, `sync.c` | runqueue |
| `kernel/fdt/` | read-only DTB parser | yes |
| `kernel/shell/` | `shell_parse.c` (line editor, tokenizer; portable), `shell.c` | parser |
| `kernel/lib/` | string, printf, ESR decoder, scause decoder, Sv39 PTE builder/walker, chainloader protocol + CRC-32 | yes |
| `chainloader/` | serial bootloader | protocol logic |

"Portable" files include no hardware headers. They are compiled for the
kernel (`aarch64-freestanding`, `riscv64-freestanding`) and natively for the
unit tests in `tests/`.

Architecture code sits behind small interfaces instead of `#ifdef`s in the
portable files: `build.py` puts `kernel/arch/<arch>/include` first on the
include path, so `#include "arch.h"` (MMIO, IRQ masking, counters, mode
names) and `"context.h"` (`struct cpu_ctx`, `struct trapframe`, syscall
argument accessors) resolve per architecture. `hal.h` declares the few
hooks the shell and `kernel_main` need (`arch_report_entry`, `arch_info`,
`system_reboot`, `system_poweroff`), and `drivers.h` the UART,
interrupt-controller (`irqchip_init/enable`) and tick APIs that each
board implements. The only remaining `#if`s are the arch-specific fields
of `struct platform`, the DT probes in `platform.c` and the system-call
instruction in the user program.

## Boot flow

```mermaid
sequenceDiagram
    participant FW as Firmware (Pi 5 EEPROM + TF-A / QEMU)
    participant B as boot.S
    participant K as kernel_main
    FW->>B: x0 = DTB, MMU off, EL2 (Pi 5 / QEMU virtualization=on)
    B->>B: Linux Image header (text_offset 0x80000)
    B->>B: MPIDR Aff0-2 != 0 -> park in WFE (PSCI keeps secondaries off anyway)
    B->>B: self-relocate if load addr != link addr
    B->>B: EL2: HCR_EL2.RW, CNTHCTL_EL2 timer access, CPTR_EL2, SCTLR_EL1, ERET
    B->>B: EL1: FP on, SP, zero BSS, VBAR_EL1
    B->>K: kernel_main(dtb, boot_el)
    K->>K: board defaults -> UART -> banner
    K->>K: FDT: /memory, stdout-path/pl011, gic-400, armv8-timer, psci
    K->>K: MMU on (TTBR0 identity, TTBR1 higher-half alias)
    K->>K: buddy over free RAM (minus DTB, /memreserve/), kheap
    K->>K: GIC, scheduler (boot context = idle), timer, UART RX IRQ
    K->>K: create shell thread, enable IRQs, idle: reap + WFI
```

With the chainloader, the firmware starts `chainloader.img` instead. It
copies itself to `CHAIN_BASE`, receives the kernel over the UART into the
original load address, and jumps there with the firmware's `x0` (DTB) and
the original exception level. From then on the flow above is identical.

## Exceptions and interrupts

* `vectors.S` has 16 entries. Each saves a 272-byte trap frame (x0-x30,
  SP_EL0, ELR_EL1, SPSR_EL1) on the current thread's kernel stack and calls
  `exception_dispatch(tf, type)`.
* **IRQ** (EL1 or EL0): `GICC_IAR` → timer or UART handler → `GICC_EOIR` →
  `sched_preempt_point()`. A context switch inside the IRQ path is safe
  because every thread has its own kernel stack. The interrupted frame stays
  on the old stack until that thread is switched back in.
* **EL0 sync**: EC 0x15 (SVC64) goes to `syscall_dispatch`. Anything else is
  decoded and printed, and the task is killed.
* **EL1 sync**: decoded (`esr.c` names the class, fault status code, level
  and read/write) and printed with ELR and FAR. If a thread other than
  idle/boot faulted, only that thread is killed. Otherwise the kernel panics.

## Scheduler

* `struct thread` array (32 slots). Each thread has a 16 KiB kernel stack from the
  buddy allocator and a `cpu_ctx` of callee-saved registers, saved and restored
  by `cpu_switch`.
* Run queue: 8 FIFO levels plus a bitmap (`runqueue.c`). Priority 0 is the highest.
  The highest non-empty level runs, and threads in the same level take turns with a
  5-tick (50 ms) slice. A thread that wakes with a higher priority than the running
  thread preempts it at the next preempt point. Priorities are strict, so starvation
  is possible by design for now.
* The idle thread is the boot context. It is never queued and runs only when
  the run queue is empty. It frees zombie stacks and executes `WFI`.
* Sleep uses tick deadlines that `sched_tick()` scans. Semaphores keep FIFO
  wait queues built on the same list node as the run queue. A mutex is a
  binary semaphore plus an owner check.
* EL0 tasks are kernel threads that `eret` to EL0 through `enter_user()`. They
  have a 4 KiB user stack in the `.user.stack` section and run code from the
  `.user` section. Both sections are mapped with EL0 permissions. Address
  spaces are still shared (see roadmap).

## Memory management

See [memory-map.md](memory-map.md) for addresses.

* **Buddy allocator**: orders 0-10 (4 KiB to 4 MiB). It keeps one metadata byte per
  page, and the free lists are stored inside the free pages. Pages start out
  reserved, and only the ranges passed to `buddy_add_range()` become usable,
  which is how the DTB and `/memreserve/` entries are excluded.
* **kheap**: slab-style size classes 16..1024 B carved from single pages (a
  64 B page header holds magic, class and live count). Larger requests get
  2^n pages directly. `kfree()` finds the header by masking the pointer.
  Slab pages are not yet returned to the buddy allocator.
* **MMU**: 4 KiB granule, T0SZ=T1SZ=16 (48-bit), 4 levels. MAIR holds
  Device-nGnRnE, Device-nGnRE, Normal WB and Normal NC. The kernel image is
  mapped with 4 KiB pages so that each section gets its own permissions:

  | Range | EL1 | EL0 |
  |-------|-----|-----|
  | `.text.boot`, `.text` | R-X | --- |
  | `.user` (user code + strings) | R-- (PXN) | R-X |
  | `.rodata` | R-- | --- |
  | `.data`, `.bss`, boot stack | RW- | --- |
  | `.user_stack` | RW- | RW- (XN) |
  | rest of RAM | RW- (2 MiB blocks) | --- |
  | device window | Device-nGnRE, XN | --- |

  TTBR1 maps all RAM again at `0xffff_0000_0000_0000 + PA`. The kernel still
  runs from the identity map. The alias is checked at boot by writing through
  one mapping, reading through the other, and comparing with `AT S1E1R`. This
  is the groundwork for linking the kernel high and handing TTBR0 to
  per-process tables.

## Platform description

`board_<name>.c` holds compile-time defaults. `platform.c` then refines them
from the DTB:

* memory: `/memory` `reg[0]`, translated through any parent `ranges`
* console: `/chosen/stdout-path` (alias names are resolved), accepted only
  if it is a PL011 inside the board's device window. Otherwise the first
  enabled PL011 in that window is used. This rule matters on the Pi 5: its
  `serial0` may point to an RP1 UART behind PCIe, which is not reachable yet.
* GIC: `arm,gic-400` or `arm,cortex-a15-gic`, with `reg[0]` as the
  distributor and `reg[1]` as the CPU interface. GICv3 trees are detected
  and reported as unsupported.
* timer PPI (entry 1 of `arm,armv8-timer`) and the PSCI conduit (`method`).

## RV64 port

The `rv64` board targets the orbit-rv SoC (RV64IMA_Zicsr_Zifencei, no C,
no F/D, 256 KiB of FPGA block RAM) and the matching subset of QEMU's
riscv64 `virt` machine. Everything above the arch layer is shared with
the AArch64 boards: scheduler, sync, buddy/kheap, FDT, shell, demos and
the user program.

| Concern | AArch64 | RV64 |
|---------|---------|------|
| Entry | EL2 or EL1, Linux Image header | M-mode at `0x8000_0000`, `a0` = hartid, `a1` = DTB or 0 |
| Firmware | PSCI (TF-A / QEMU) | built-in SBI shim in M-mode ([sbi.md](sbi.md)) |
| Kernel mode / user mode | EL1 / EL0 | S-mode / U-mode |
| Trap entry | 16-entry `VBAR_EL1` table, 272-byte frame | `stvec` direct, 288-byte frame (x1-x31, sstatus, sepc, stval, scause) |
| Kernel stack on user trap | `SP_EL1` | `sscratch` holds the kernel stack top while in U-mode, 0 in S-mode |
| Fault decoder | `esr.c` | `scause.c` |
| Translation | 4-level, 48-bit VA, MAIR types | Sv39, A/D bits pre-set (orbit-rv is Svade) |
| Timer | CNTP, PPI 30 | SBI `set_timer` → CLINT `mtimecmp` → STIP |
| Interrupt controller | GICv2 | PLIC, context 1 (hart 0 S-mode) |
| UART | PL011 | NS16550 (byte registers, PLIC source 10) |
| Syscall | `svc #0`, x8 = number | `ecall`, a7 = number (same numbers) |
| Power | PSCI SYSTEM_OFF/RESET | SBI SRST → finisher `0x5555`/`0x7777` |
| Kernel stacks | 16 KiB | 8 KiB (`KSTACK_ORDER=1`, RAM budget) |

Boot on rv64: `boot.S` (M-mode) parks other harts, clears BSS, installs the
SBI trap vector and stack, opens PMP, delegates exceptions and the S-level
interrupts, and `mret`s into `kernel_main` in S-mode with `stvec` set. From
there the sequence is the same as on AArch64: UART, banner, FDT (QEMU only;
orbit-rv passes no DTB), Sv39 on, buddy + kheap, PLIC, scheduler, timer,
UART RX interrupt, shell.

Traps: `entry.S` saves the frame and calls `trap_dispatch()`. Supervisor
timer and external interrupts go to the tick and the PLIC claim loop, then
`sched_preempt_point()`. `ecall` from U advances `sepc` and enters
`syscall_dispatch()`. Any other exception is decoded by `scause.c` and kills
the thread that caused it (a U-mode task or a kernel thread), or panics in
the idle context. `SYS_WRITE` reads the user buffer with `sstatus.SUM` set
only around the copy (`user_access_begin/end`); the pointer is validated
against the user sections first, as on AArch64.

Sv39 layout (`arch/riscv64/mmu.c`, built with `lib/sv39.c`):

| Range | S-mode | U-mode | Page size |
|-------|--------|--------|-----------|
| `.text.boot`, `.text` | R-X | --- | 4 KiB |
| `.user` (user code + strings) | --- (U page, S never executes it; SUM to read) | R-X | 4 KiB |
| `.rodata` | R-- | --- | 4 KiB |
| `.data`, `.bss`, stacks, page tables | RW- | --- | 4 KiB |
| `.user_stack` | --- (SUM to touch) | RW- | 4 KiB |
| rest of the 256 KiB RAM | RW- | --- | 4 KiB |
| DTB (QEMU) | R-- | --- | 2 MiB, rounded out |
| PLIC, UART | RW- | --- | 2 MiB, rounded out |
| higher-half alias `0xffff_ffc0_0000_0000 + PA` | RW- | --- | 2 MiB |

Every leaf has A and D set when it is created, because the orbit-rv CPU
raises a page fault instead of updating them. `mmu_translate()` (used by
`peek`/`poke`) is a software walk that answers "would a kernel read
fault?", like `AT S1E1R`: U pages count as not readable. Five 4 KiB tables
are used (root, two L1 for the device and RAM gigabytes, one L0 for the
kernel image, one L1 for the alias).
