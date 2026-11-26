/* Device drivers. The UART, interrupt-controller and tick APIs are the
 * same on every board; the implementation is picked by the build:
 *   AArch64: PL011 UART, GICv2 (GIC-400), ARM generic timer, PSCI
 *   RV64:    NS16550 UART, PLIC, SBI timer (CLINT behind the SBI shim) */
#ifndef ORBIT_DRIVERS_H
#define ORBIT_DRIVERS_H

#include <stdint.h>

/* UART (pl011.c / ns16550.c) */
void uart_init(uint64_t base, uint32_t clock_hz, uint32_t baud);
void uart_putc(char c);
const char *uart_name(void);
void uart_enable_rx_irq(void);

/* Hardware hooks of the UART drivers, used by serial.c. */
int  uart_hw_getc(void);        /* -1 if the RX FIFO is empty */
void uart_hw_enable_rx_irq(void);
void uart_hw_ack_rx_irq(void);

/* Interrupt-driven RX ring buffer shared by both drivers (serial.c). */
int  uart_getc_nonblock(void);  /* -1 if no byte */
void uart_irq_handler(void);    /* drains the RX FIFO into the ring buffer */
int  uart_rx_irq_mode(void);
/* Blocking read for threads: sleeps on a semaphore (IRQ mode) or polls
 * with 10 ms sleeps (poll mode). */
char uart_getc_blocking(void);

/* Interrupt controller (gic.c / plic.c): set up from `plat`, print a
 * boot line, then enable individual lines. */
void irqchip_init(void);
void irqchip_enable(int irq);

/* GICv2 (AArch64) */
void gic_init(uint64_t gicd, uint64_t gicc);
void gic_enable_irq(int intid);
uint32_t gic_ack(void);         /* GICC_IAR */
void gic_eoi(uint32_t iar);
uint32_t gic_num_lines(void);

/* PLIC (RV64), hart 0 S-mode context */
void plic_irq(void);            /* claim, dispatch and complete pending sources */
uint32_t plic_num_sources(void);

/* Periodic tick (timer.c / sbi_timer.c), bookkeeping in clock.c */
#define HZ 100
void timer_init(void);          /* program the first tick, print a boot line */
void timer_irq(void);
void clock_init(void);          /* uptime starts here */
void clock_tick(void);          /* one tick: count it, run the scheduler tick */
uint64_t timer_ticks(void);
uint64_t uptime_ms(void);
void delay_us(uint64_t us);

/* PSCI (AArch64) */
#define PSCI_VERSION       0x84000000u
#define PSCI_SYSTEM_OFF    0x84000008u
#define PSCI_SYSTEM_RESET  0x84000009u
int64_t psci_call(uint32_t fn, uint64_t a1, uint64_t a2, uint64_t a3);

/* Top-level IRQ dispatch (called from the AArch64 vector table). */
void irq_dispatch(void);

#endif
