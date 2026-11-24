/* Console output on the UART, plus panic() and the libc symbols that the
 * compiler may emit calls to (memcpy/memset/...). */
#include <stdarg.h>
#include "arch.h"
#include "drivers.h"
#include "kernel.h"

void console_write(const char *s, size_t n)
{
    /* Mask IRQs so lines from different threads do not interleave. */
    uint64_t f = irq_save();
    size_t i;
    for (i = 0; i < n; i++) {
        if (s[i] == '\n')
            uart_putc('\r');
        uart_putc(s[i]);
    }
    irq_restore(f);
}

void kputs(const char *s)
{
    console_write(s, k_strlen(s));
}

void kprintf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = k_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > (int)sizeof(buf) - 1)
        n = sizeof(buf) - 1;
    console_write(buf, (size_t)n);
}

void panic(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    irq_disable();
    va_start(ap, fmt);
    k_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    kputs("\n*** KERNEL PANIC: ");
    kputs(buf);
    kputs("\n*** system halted\n");
    for (;;)
        wfi();
}

/* Compiler-emitted builtins. */
void *memcpy(void *d, const void *s, size_t n) { return k_memcpy(d, s, n); }
void *memmove(void *d, const void *s, size_t n) { return k_memmove(d, s, n); }
void *memset(void *d, int c, size_t n) { return k_memset(d, c, n); }
int memcmp(const void *a, const void *b, size_t n) { return k_memcmp(a, b, n); }
