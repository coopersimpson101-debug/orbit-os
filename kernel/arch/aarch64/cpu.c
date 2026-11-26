/* AArch64 implementation of the hal.h hooks. */
#include "arch.h"
#include "drivers.h"
#include "hal.h"
#include "kernel.h"

void arch_report_entry(uint64_t dtb, uint64_t boot_el)
{
    kprintf("[boot] entered at EL%lu, now at EL%lu, dtb=%p, image %p-%p\n", boot_el,
            (read_sysreg(CurrentEL) >> 2) & 3, (void *)dtb, __kernel_start, _end);
}

void arch_info(void)
{
    kprintf("gic      : GICv2 D=%p C=%p, %u lines (%s)\n", (void *)plat.gicd_base,
            (void *)plat.gicc_base, gic_num_lines(), plat.src_gic);
    kprintf("timer    : INTID %d, CNTFRQ %lu Hz\n", plat.timer_irq, cpu_counter_freq());
    kprintf("psci     : %s (%s)\n",
            plat.psci == PSCI_SMC ? "smc" : plat.psci == PSCI_HVC ? "hvc" : "none",
            plat.src_psci);
    kprintf("dtb      : %p (%u bytes)\n", plat.dtb, plat.dtb_size);
    kprintf("cpu      : MIDR_EL1=0x%lx MPIDR_EL1=0x%lx\n", read_sysreg(midr_el1),
            read_sysreg(mpidr_el1));
}

void system_reboot(void)
{
    psci_call(PSCI_SYSTEM_RESET, 0, 0, 0);
}

void system_poweroff(void)
{
    psci_call(PSCI_SYSTEM_OFF, 0, 0, 0);
}
