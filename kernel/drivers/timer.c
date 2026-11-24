/* ARM generic timer: EL1 non-secure physical timer (CNTP_*), HZ ticks.
 * boot.S grants EL1 access via CNTHCTL_EL2 when we start at EL2. */
#include "arch.h"
#include "drivers.h"
#include "kernel.h"
#include "sched.h"

static uint64_t tick_interval;
static volatile uint64_t ticks;
static int timer_intid;
static uint64_t boot_count; /* firmware may not start CNTPCT at 0 */

void timer_init(int intid)
{
    uint64_t frq = cpu_counter_freq();
    timer_intid = intid;
    boot_count = cpu_counter();
    tick_interval = frq / HZ;
    write_sysreg(cntp_tval_el0, tick_interval);
    write_sysreg(cntp_ctl_el0, 1); /* ENABLE, not masked */
    isb();
    gic_enable_irq(intid);
}

void timer_irq(void)
{
    write_sysreg(cntp_tval_el0, tick_interval);
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

/* SMCCC v1.0 lets firmware clobber x4-x17. */
#define SMCCC_CLOBBERS "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", \
    "x13", "x14", "x15", "x16", "x17"

int64_t psci_call(uint32_t fn, uint64_t a1, uint64_t a2, uint64_t a3)
{
    register uint64_t x0 __asm__("x0") = fn;
    register uint64_t x1 __asm__("x1") = a1;
    register uint64_t x2 __asm__("x2") = a2;
    register uint64_t x3 __asm__("x3") = a3;
    if (plat.psci == PSCI_SMC)
        __asm__ volatile("smc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3)
                         : "memory", SMCCC_CLOBBERS);
    else if (plat.psci == PSCI_HVC)
        __asm__ volatile("hvc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3)
                         : "memory", SMCCC_CLOBBERS);
    else
        return -1; /* NOT_SUPPORTED */
    return (int64_t)x0;
}

void irq_dispatch(void)
{
    uint32_t iar = gic_ack();
    uint32_t id = iar & 0x3ff;
    if (id >= 1020)
        return; /* spurious */
    if ((int)id == timer_intid)
        timer_irq();
    else if ((int)id == plat.uart_irq)
        uart_irq_handler();
    else
        kprintf("[irq] unexpected INTID %u\n", id);
    gic_eoi(iar);
}
