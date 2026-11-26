/* Tick counting and time since boot, on top of the architecture's free-
 * running counter (cpu_counter(): CNTPCT_EL0 / the RISC-V time CSR). */
#include "arch.h"
#include "drivers.h"
#include "sched.h"

static volatile uint64_t ticks;
static uint64_t boot_count; /* the counter need not start at 0 */

void clock_init(void)
{
    boot_count = cpu_counter();
}

void clock_tick(void)
{
    ticks++;
    sched_tick();
}

uint64_t timer_ticks(void)
{
    return ticks;
}

uint64_t uptime_ms(void)
{
    uint64_t frq = cpu_counter_freq();
    return frq ? (cpu_counter() - boot_count) / (frq / 1000) : 0;
}

void delay_us(uint64_t us)
{
    uint64_t frq = cpu_counter_freq();
    uint64_t end = cpu_counter() + us * (frq / 1000000);
    while (cpu_counter() < end)
        ;
}
