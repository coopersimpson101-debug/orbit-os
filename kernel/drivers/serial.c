/* UART receive path shared by the PL011 and NS16550 drivers: in IRQ mode
 * the RX interrupt drains the hardware FIFO into a ring buffer and wakes
 * readers through a semaphore; otherwise readers poll the FIFO. */
#include "arch.h"
#include "drivers.h"
#include "sched.h"
#include "sync.h"

#define RX_RING 256
static volatile char rx_ring[RX_RING];
static volatile unsigned rx_head, rx_tail;
static struct semaphore rx_sem;
static int irq_mode;

void uart_enable_rx_irq(void)
{
    sem_init(&rx_sem, 0);
    uart_hw_enable_rx_irq();
    irq_mode = 1;
}

int uart_rx_irq_mode(void)
{
    return irq_mode;
}

int uart_getc_nonblock(void)
{
    if (irq_mode) {
        uint64_t f = irq_save();
        int c = -1;
        if (rx_head != rx_tail) {
            c = (unsigned char)rx_ring[rx_tail % RX_RING];
            rx_tail++;
        }
        irq_restore(f);
        return c;
    }
    return uart_hw_getc();
}

void uart_irq_handler(void)
{
    int c;
    while ((c = uart_hw_getc()) >= 0) {
        if (rx_head - rx_tail < RX_RING) {
            rx_ring[rx_head % RX_RING] = (char)c;
            rx_head++;
            sem_up(&rx_sem);
        }
    }
    uart_hw_ack_rx_irq();
}

char uart_getc_blocking(void)
{
    for (;;) {
        int c;
        if (irq_mode)
            sem_down(&rx_sem);
        c = uart_getc_nonblock();
        if (c >= 0)
            return (char)c;
        if (!irq_mode)
            thread_sleep_ms(10);
    }
}
