/* RV64 tick: absolute deadlines on the `time` CSR, programmed through SBI
 * set_timer. The M-mode shim writes CLINT mtimecmp and forwards the
 * machine timer interrupt as STIP, which arrives here as a supervisor
 * timer interrupt (delegated via mideleg). */
#include "arch.h"
#include "drivers.h"
#include "kernel.h"
#include "sbi.h"

static uint64_t tick_interval, next_deadline;

void timer_init(void)
{
    uint64_t frq = cpu_counter_freq();
    clock_init();
    tick_interval = frq / HZ;
    next_deadline = cpu_counter() + tick_interval;
    sbi_set_timer(next_deadline);
    csr_set(sie, SIE_STIE);
    kprintf("[timer] %d Hz tick via SBI set_timer (timebase %lu Hz, %lu ticks/interval)\n",
            HZ, frq, tick_interval);
}

void timer_irq(void)
{
    uint64_t now = cpu_counter();
    /* Fixed-rate deadlines; if we fell behind (long IRQ-off section),
     * skip the missed ones rather than storming. */
    next_deadline += tick_interval;
    if ((int64_t)(next_deadline - now) <= 0)
        next_deadline = now + tick_interval;
    sbi_set_timer(next_deadline); /* also clears STIP */
    clock_tick();
}
