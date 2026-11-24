/* Raspberry Pi 5 (BCM2712, 4x Cortex-A76, RP1 south bridge over PCIe).
 *
 * Addresses below are from the upstream/raspberrypi Linux bcm2712 device
 * trees as best understood by the authors - NOT yet verified on hardware
 * (see docs/hardware-validation.md). The firmware's DTB, when present,
 * overrides them, constrained to the SoC device window below.
 *
 *  - Debug UART (3-pin JST "UART" header between the HDMI ports): ARM
 *    PL011 at 0x10_7d00_1000 (Linux: earlycon=pl011,0x107d001000,115200n8).
 *    The 40-pin GPIO14/15 UARTs live in RP1 behind PCIe and need a PCIe +
 *    RP1 driver, so they are out of scope for now.
 *  - GIC-400: distributor 0x10_7fff_9000, CPU interface 0x10_7fff_a000.
 *  - PSCI via SMC to the TF-A BL31 bundled with the Pi 5 firmware.
 */
#include "kernel.h"

void board_defaults(struct platform *p, int boot_el)
{
    (void)boot_el;
    p->board = "rpi5";
    p->ram_base = 0;
    p->ram_size = 0x3c000000ULL; /* conservative 960 MiB until the DTB says more */
    p->uart_base = 0x107d001000ULL;
    p->uart_clock = 0;           /* keep firmware baud setup (enable_uart=1) */
    p->uart_irq = -1;            /* poll unless the DTB gives us the SPI */
    p->gicd_base = 0x107fff9000ULL;
    p->gicc_base = 0x107fffa000ULL;
    p->timer_irq = 30;
    p->psci = PSCI_SMC;
    p->dev_base = 0x107c000000ULL;
    p->dev_size = 0x04000000ULL; /* 0x10_7c00_0000 .. 0x10_8000_0000 */
    p->src_mem = p->src_uart = p->src_gic = p->src_psci = "board default";
}
