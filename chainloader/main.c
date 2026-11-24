/* orbit-chainloader: receive a kernel image over the UART and boot it.
 * Protocol: kernel/include/chainproto.h, host side: tools/send_kernel.py. */
#include <stdint.h>
#include "chainproto.h"

#ifndef UART_BASE
#error "UART_BASE must be defined by the build"
#endif
#ifndef UART_CLOCK
#define UART_CLOCK 0
#endif

#define DR   0x00
#define FR   0x18
#define IBRD 0x24
#define FBRD 0x28
#define LCRH 0x2c
#define CR   0x30
#define FR_TXFF (1u << 5)
#define FR_RXFE (1u << 4)

extern char _start[];
void chain_jump(uint64_t entry, uint64_t dtb) __attribute__((noreturn));

static inline uint32_t rd(uint32_t off)
{
    return *(volatile uint32_t *)(uintptr_t)(UART_BASE + off);
}

static inline void wr(uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(uintptr_t)(UART_BASE + off) = v;
}

static void putc_raw(char c)
{
    while (rd(FR) & FR_TXFF)
        ;
    wr(DR, (uint32_t)(unsigned char)c);
}

static void puts_raw(const char *s)
{
    while (*s) {
        if (*s == '\n')
            putc_raw('\r');
        putc_raw(*s++);
    }
}

static void puthex(uint64_t v)
{
    int i;
    puts_raw("0x");
    for (i = 60; i >= 0; i -= 4)
        putc_raw("0123456789abcdef"[(v >> i) & 0xf]);
}

static uint8_t getc_raw(void)
{
    while (rd(FR) & FR_RXFE)
        ;
    return (uint8_t)(rd(DR) & 0xff);
}

static void uart_setup(void)
{
#if UART_CLOCK
    uint32_t div64 = (uint32_t)(((uint64_t)UART_CLOCK * 4 + 115200 / 2) / 115200);
    wr(CR, 0);
    wr(IBRD, div64 >> 6);
    wr(FBRD, div64 & 63);
    wr(LCRH, (3u << 5) | (1u << 4));
    wr(CR, (1u << 0) | (1u << 8) | (1u << 9));
#endif
}

void chain_main(uint64_t dtb, uint64_t load_addr)
{
    /* Everything between the load address and our own relocated copy
     * (minus a 64 KiB guard) can hold the incoming kernel. */
    uint64_t max = (uint64_t)(uintptr_t)_start - load_addr - 0x10000;
    struct cl_rx st;

    uart_setup();
    for (;;) {
        puts_raw("\n[orbit-chainloader] ready. load=");
        puthex(load_addr);
        puts_raw(" max=");
        puthex(max);
        puts_raw(" dtb=");
        puthex(dtb);
        puts_raw("\n[orbit-chainloader] run: tools/send_kernel.py <port> kernel8.img\n");
        putc_raw(3);
        putc_raw(3);
        putc_raw(3);

        cl_rx_init(&st, (uint8_t *)(uintptr_t)load_addr, (uint32_t)max);
        for (;;) {
            enum cl_event ev = cl_rx_feed(&st, getc_raw());
            if (ev == CL_MORE)
                continue;
            if (ev == CL_HEADER_OK) {
                putc_raw('O');
                putc_raw('K');
                continue;
            }
            if (ev == CL_HEADER_BAD) {
                putc_raw('S');
                putc_raw('Z');
                break;
            }
            if (ev == CL_DONE_BADCRC) {
                putc_raw('C');
                putc_raw('K');
                break;
            }
            /* CL_DONE_OK */
            putc_raw('O');
            putc_raw('K');
            puts_raw("\n[orbit-chainloader] CRC OK, jumping to ");
            puthex(load_addr);
            puts_raw("\n");
            while (!(rd(FR) & (1u << 7))) /* wait for TX FIFO empty (TXFE) */
                ;
            chain_jump(load_addr, dtb);
        }
    }
}
