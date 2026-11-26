/* NS16550-compatible UART with byte-wide registers at stride 1, as on the
 * orbit-rv SoC and QEMU riscv64 virt (0x1000_0000, PLIC source 10). */
#include "arch.h"
#include "drivers.h"

#define RBR 0 /* read, DLAB = 0 */
#define THR 0 /* write, DLAB = 0 */
#define DLL 0 /* DLAB = 1 */
#define IER 1
#define DLM 1 /* DLAB = 1 */
#define FCR 2 /* write */
#define LCR 3
#define MCR 4
#define LSR 5

#define IER_RDA    0x01 /* received data available */
#define FCR_ENABLE 0x01
#define FCR_CLR_RX 0x02
#define FCR_CLR_TX 0x04
#define LCR_8N1    0x03
#define LCR_DLAB   0x80
#define MCR_DTR_RTS_OUT2 0x0b
#define LSR_DR     0x01
#define LSR_THRE   0x20

static uint64_t uart_base;

void uart_init(uint64_t base, uint32_t clock_hz, uint32_t baud)
{
    uart_base = base;
    mmio_write8(base + IER, 0);
    if (clock_hz) {
        /* divisor = round(clk / (16 * baud)): 25 MHz / 115200 -> 14 (-3.1 %). */
        uint32_t div = (clock_hz + 8 * baud) / (16 * baud);
        mmio_write8(base + LCR, LCR_DLAB);
        mmio_write8(base + DLL, (uint8_t)(div & 0xff));
        mmio_write8(base + DLM, (uint8_t)(div >> 8));
    }
    mmio_write8(base + LCR, LCR_8N1);
    mmio_write8(base + FCR, FCR_ENABLE | FCR_CLR_RX | FCR_CLR_TX); /* RX trigger: 1 byte */
    mmio_write8(base + MCR, MCR_DTR_RTS_OUT2); /* OUT2 gates the IRQ on PC-style 16550s */
}

const char *uart_name(void)
{
    return "NS16550";
}

void uart_putc(char c)
{
    if (!uart_base)
        return;
    while (!(mmio_read8(uart_base + LSR) & LSR_THRE))
        ;
    mmio_write8(uart_base + THR, (uint8_t)c);
}

int uart_hw_getc(void)
{
    if (!(mmio_read8(uart_base + LSR) & LSR_DR))
        return -1;
    return mmio_read8(uart_base + RBR);
}

void uart_hw_enable_rx_irq(void)
{
    mmio_write8(uart_base + IER, IER_RDA);
}

void uart_hw_ack_rx_irq(void)
{
    /* Level-triggered: the RDA condition clears once the FIFO is empty. */
}
