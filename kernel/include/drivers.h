/* Device drivers: PL011 UART, GICv2 (GIC-400), ARM generic timer, PSCI. */
#ifndef ORBIT_DRIVERS_H
#define ORBIT_DRIVERS_H

#include <stdint.h>

/* PL011 */
void uart_init(uint64_t base, uint32_t clock_hz, uint32_t baud);
void uart_putc(char c);
int  uart_getc_nonblock(void);  /* -1 if no byte */
void uart_enable_rx_irq(void);
void uart_irq_handler(void);    /* drains RX FIFO into a ring buffer */
int  uart_rx_irq_mode(void);
/* Blocking read for threads: sleeps on a semaphore (IRQ mode) or polls
 * with 10 ms sleeps (poll mode). */
char uart_getc_blocking(void);

/* GICv2 */
void gic_init(uint64_t gicd, uint64_t gicc);
void gic_enable_irq(int intid);
uint32_t gic_ack(void);         /* GICC_IAR */
void gic_eoi(uint32_t iar);
uint32_t gic_num_lines(void);

/* Generic timer */
#define HZ 100
void timer_init(int intid);
void timer_irq(void);
uint64_t timer_ticks(void);
uint64_t uptime_ms(void);
void delay_us(uint64_t us);

/* PSCI */
#define PSCI_VERSION       0x84000000u
#define PSCI_SYSTEM_OFF    0x84000008u
#define PSCI_SYSTEM_RESET  0x84000009u
int64_t psci_call(uint32_t fn, uint64_t a1, uint64_t a2, uint64_t a3);

/* Top-level IRQ dispatch (called from the vector table). */
void irq_dispatch(void);

#endif
