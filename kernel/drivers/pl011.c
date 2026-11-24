/* ARM PrimeCell PL011 UART. On the Raspberry Pi 5 this is the BCM2712
 * debug UART on the 3-pin JST header; on QEMU virt it is the only UART. */
#include "arch.h"
#include "drivers.h"
#include "sched.h"
#include "sync.h"

#define DR    0x00
#define FR    0x18
#define IBRD  0x24
#define FBRD  0x28
#define LCRH  0x2c
#define CR    0x30
#define IMSC  0x38
#define ICR   0x44

#define FR_TXFF (1u << 5)
#define FR_RXFE (1u << 4)
#define FR_BUSY (1u << 3)

static uint64_t uart_base;
static int irq_mode;

#define RX_RING 256
static volatile char rx_ring[RX_RING];
static volatile unsigned rx_head, rx_tail;
static struct semaphore rx_sem;

void uart_init(uint64_t base, uint32_t clock_hz, uint32_t baud)
{
    uart_base = base;
    if (clock_hz) {
        /* Divisor = clock / (16 * baud), fractional part in 1/64ths. */
        uint32_t div64 = (uint32_t)(((uint64_t)clock_hz * 4 + baud / 2) / baud);
        mmio_write32(base + CR, 0);
        while (mmio_read32(base + FR) & FR_BUSY)
            ;
        mmio_write32(base + ICR, 0x7ff);
        mmio_write32(base + IBRD, div64 >> 6);
        mmio_write32(base + FBRD, div64 & 63);
        mmio_write32(base + LCRH, (3u << 5) | (1u << 4)); /* 8N1, FIFOs on */
        mmio_write32(base + CR, (1u << 0) | (1u << 8) | (1u << 9));
    }
    /* clock_hz == 0: the firmware already configured the line (Pi 5
     * enable_uart=1); we deliberately do not touch the divisors because
     * the debug UART's reference clock is not documented publicly. */
    sem_init(&rx_sem, 0);
}

void uart_putc(char c)
{
    if (!uart_base)
        return;
    while (mmio_read32(uart_base + FR) & FR_TXFF)
        ;
    mmio_write32(uart_base + DR, (uint32_t)(unsigned char)c);
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
    if (mmio_read32(uart_base + FR) & FR_RXFE)
        return -1;
    return (int)(mmio_read32(uart_base + DR) & 0xff);
}

void uart_enable_rx_irq(void)
{
    /* RX and RX-timeout interrupts so we hear about every byte. */
    mmio_write32(uart_base + IMSC, (1u << 4) | (1u << 6));
    irq_mode = 1;
}

int uart_rx_irq_mode(void)
{
    return irq_mode;
}

void uart_irq_handler(void)
{
    while (!(mmio_read32(uart_base + FR) & FR_RXFE)) {
        char c = (char)(mmio_read32(uart_base + DR) & 0xff);
        if (rx_head - rx_tail < RX_RING) {
            rx_ring[rx_head % RX_RING] = c;
            rx_head++;
            sem_up(&rx_sem);
        }
    }
    mmio_write32(uart_base + ICR, (1u << 4) | (1u << 6));
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
