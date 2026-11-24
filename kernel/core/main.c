/* kernel_main: called from boot.S on the boot core at EL1, MMU off. */
#include "arch.h"
#include "drivers.h"
#include "kernel.h"
#include "mm.h"
#include "sched.h"
#include "demos.h"

void kernel_main(uint64_t dtb, uint64_t boot_el)
{
    uint64_t early_uart;

    board_defaults(&plat, (int)boot_el);
    uart_init(plat.uart_base, plat.uart_clock, 115200);
    early_uart = plat.uart_base;

    kprintf("\n\nOrbitOS v%s (%s) - hobby AArch64 kernel\n", ORBIT_VERSION, plat.board);
    kprintf("[boot] entered at EL%lu, now at EL%lu, dtb=%p, image %p-%p\n", boot_el,
            (read_sysreg(CurrentEL) >> 2) & 3, (void *)dtb, __kernel_start, _end);

    platform_probe_fdt(&plat, dtb);
    if (plat.uart_base != early_uart) {
        kprintf("[boot] switching console to DT UART at %p\n", (void *)plat.uart_base);
        uart_init(plat.uart_base, plat.uart_clock, 115200);
    }
    kprintf("[boot] RAM %p + %lu MiB (%s)\n", (void *)plat.ram_base, plat.ram_size >> 20,
            plat.src_mem);

    mmu_init();
    kprintf("[mmu] enabled: identity map + kernel sections at 4K, devices nGnRE, "
            "%lu tables\n", (uint64_t)mmu_tables_used());

    mm_init();
    kprintf("[mmu] higher-half alias self-test: %s\n",
            mmu_selftest_higher_half() ? "OK" : "FAILED");

    gic_init(plat.gicd_base, plat.gicc_base);
    kprintf("[gic] GICv2 at D=%p C=%p, %u interrupt lines\n", (void *)plat.gicd_base,
            (void *)plat.gicc_base, gic_num_lines());

    sched_init();
    timer_init(plat.timer_irq);
    kprintf("[timer] %d Hz tick on INTID %d (CNTFRQ %lu Hz)\n", HZ, plat.timer_irq,
            cpu_counter_freq());

    if (plat.uart_irq >= 0) {
        uart_enable_rx_irq();
        gic_enable_irq(plat.uart_irq);
    }
    kprintf("[uart] RX %s\n", plat.uart_irq >= 0 ? "interrupt driven" : "polled");

    if (!thread_create("shell", shell_thread, 0, PRIO_SHELL))
        panic("cannot create shell thread");

    kputs("[boot] boot complete, enabling interrupts\n");
    irq_enable();

    /* This context becomes the idle thread. */
    for (;;) {
        sched_reap();
        wfi();
    }
}
