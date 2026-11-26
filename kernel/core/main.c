/* kernel_main: called from boot.S on the boot core with the MMU off.
 * AArch64: at EL1, boot_mode = the EL we were entered at.
 * RV64: in S-mode after the M-mode setup, boot_mode = 3 (entered in M). */
#include "arch.h"
#include "drivers.h"
#include "hal.h"
#include "kernel.h"
#include "mm.h"
#include "sched.h"
#include "demos.h"

void kernel_main(uint64_t dtb, uint64_t boot_mode)
{
    uint64_t early_uart;

    board_defaults(&plat, (int)boot_mode);
    uart_init(plat.uart_base, plat.uart_clock, 115200);
    early_uart = plat.uart_base;

    kprintf("\n\nOrbitOS v%s (%s) - hobby %s kernel\n", ORBIT_VERSION, plat.board, ARCH_NAME);
    arch_report_entry(dtb, boot_mode);

    platform_probe_fdt(&plat, dtb);
    if (plat.uart_base != early_uart) {
        kprintf("[boot] switching console to DT UART at %p\n", (void *)plat.uart_base);
        uart_init(plat.uart_base, plat.uart_clock, 115200);
    }
    kprintf("[boot] RAM %p + %lu %s (%s)\n", (void *)plat.ram_base,
            plat.ram_size >= (1ULL << 20) ? plat.ram_size >> 20 : plat.ram_size >> 10,
            plat.ram_size >= (1ULL << 20) ? "MiB" : "KiB", plat.src_mem);

    mmu_init();
    kprintf("[mmu] enabled: " MMU_BOOT_SUMMARY ", %lu tables\n", (uint64_t)mmu_tables_used());

    mm_init();
    kprintf("[mmu] higher-half alias self-test: %s\n",
            mmu_selftest_higher_half() ? "OK" : "FAILED");

    irqchip_init();
    sched_init();
    timer_init();

    if (plat.uart_irq >= 0) {
        uart_enable_rx_irq();
        irqchip_enable(plat.uart_irq);
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
