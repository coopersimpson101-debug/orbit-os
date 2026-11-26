/* rv64: the orbit-rv SoC (RV64IMA_Zicsr_Zifencei, FPGA block RAM) and the
 * matching subset of QEMU's riscv64 "virt" machine:
 *   qemu-system-riscv64 -M virt -bios none -m 128M -nographic -kernel kernel.elf
 * Addresses are in rv64_map.h. orbit-rv passes no DTB; under QEMU the DTB
 * refines them, but usable RAM is always capped at the compile-time
 * RAM_SIZE (256 KiB) so both machines see the same kernel. */
#include "kernel.h"
#include "rv64_map.h"

#ifndef RAM_SIZE
#error "RAM_SIZE must be defined by the build"
#endif

void board_defaults(struct platform *p, int boot_mode)
{
    (void)boot_mode;
    p->board = "rv64";
    p->machine = "orbit-rv, no DTB";
    p->ram_base = RV64_RAM_BASE;
    p->ram_size = RAM_SIZE;
    p->ram_limit = RAM_SIZE;
    p->uart_base = RV64_UART_BASE;
    p->uart_clock = UART_CLK_HZ;
    p->uart_irq = RV64_UART_IRQ;
    p->plic_base = RV64_PLIC_BASE;
    p->timebase_hz = RV64_TIMEBASE_HZ;
    p->dev_base = RV64_FINISHER_BASE;
    p->dev_size = RV64_UART_BASE + 0x1000 - RV64_FINISHER_BASE; /* .. end of the UART page */
    p->src_mem = p->src_uart = p->src_plic = p->src_timebase = "board default";
}
