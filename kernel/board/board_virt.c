/* QEMU "virt" machine (used for CI):
 *   qemu-system-aarch64 -M virt,gic-version=2,virtualization=on -cpu cortex-a72
 * Addresses from QEMU hw/arm/virt.c memmap; all of them are re-read from
 * the DTB that QEMU generates. */
#include "kernel.h"

void board_defaults(struct platform *p, int boot_el)
{
    p->board = "qemu-virt";
    p->ram_base = 0x40000000ULL;
    p->ram_size = 128ULL << 20;
    p->uart_base = 0x09000000ULL;
    p->uart_clock = 24000000; /* virt's apb-pclk; QEMU ignores the divisor anyway */
    p->uart_irq = 32 + 1;     /* SPI 1 */
    p->gicd_base = 0x08000000ULL;
    p->gicc_base = 0x08010000ULL;
    p->timer_irq = 30;        /* PPI 14: non-secure EL1 physical timer */
    /* QEMU's built-in PSCI uses SMC when EL2 is emulated, HVC otherwise. */
    p->psci = boot_el >= 2 ? PSCI_SMC : PSCI_HVC;
    p->dev_base = 0x08000000ULL;
    p->dev_size = 0x08000000ULL; /* 0x0800_0000 .. 0x1000_0000 */
    p->src_mem = p->src_uart = p->src_gic = p->src_psci = "board default";
}
