/* Physical memory map of the rv64 board: the orbit-rv SoC, which is a
 * deliberate subset of QEMU's riscv64 "virt" machine. The M-mode shim uses
 * these directly; S-mode starts from them and refines from a DTB. */
#ifndef ORBIT_RV64_MAP_H
#define ORBIT_RV64_MAP_H

#define RV64_FINISHER_BASE 0x00100000ULL /* sifive,test0 compatible */
#define RV64_CLINT_BASE    0x02000000ULL
#define RV64_CLINT_MSIP     0x0000
#define RV64_CLINT_MTIMECMP 0x4000
#define RV64_CLINT_MTIME    0xbff8
#define RV64_PLIC_BASE     0x0c000000ULL
#define RV64_PLIC_SIZE     0x00400000ULL
#define RV64_UART_BASE     0x10000000ULL /* NS16550, byte registers, stride 1 */
#define RV64_UART_IRQ      10            /* PLIC source */
#define RV64_RAM_BASE      0x80000000ULL

#define RV64_TIMEBASE_HZ   10000000u     /* mtime: 10 MHz on orbit-rv and QEMU */
#define UART_CLK_HZ        25000000u     /* orbit-rv UART input clock */

/* Finisher commands (32-bit write). */
#define FINISHER_FAIL  0x3333u           /* | exit code << 16 */
#define FINISHER_PASS  0x5555u           /* power off */
#define FINISHER_RESET 0x7777u

#endif
