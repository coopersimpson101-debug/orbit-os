/* ARM PrimeCell PL011 UART. On the Raspberry Pi 5 this is the BCM2712
 * debug UART on the 3-pin JST header; on QEMU virt it is the only UART. */
#include "arch.h"
#include "drivers.h"

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
}

const char *uart_name(void)
{
    return "PL011";
}

void uart_putc(char c)
{
    if (!uart_base)
        return;
    while (mmio_read32(uart_base + FR) & FR_TXFF)
        ;
    mmio_write32(uart_base + DR, (uint32_t)(unsigned char)c);
}

int uart_hw_getc(void)
{
    if (mmio_read32(uart_base + FR) & FR_RXFE)
        return -1;
    return (int)(mmio_read32(uart_base + DR) & 0xff);
}

void uart_hw_enable_rx_irq(void)
{
    /* RX and RX-timeout interrupts so we hear about every byte. */
    mmio_write32(uart_base + IMSC, (1u << 4) | (1u << 6));
}

void uart_hw_ack_rx_irq(void)
{
    mmio_write32(uart_base + ICR, (1u << 4) | (1u << 6));
}
