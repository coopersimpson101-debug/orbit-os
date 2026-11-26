/* RV64 implementation of the hal.h hooks. */
#include "arch.h"
#include "drivers.h"
#include "hal.h"
#include "kernel.h"
#include "sbi.h"

/* misa as read in M-mode by boot.S (S-mode cannot read it). */
uint64_t riscv_boot_misa;

uint64_t cpu_counter_freq(void)
{
    return plat.timebase_hz;
}

static const char *isa_string(uint64_t misa, char *buf, size_t size)
{
    /* Canonical extension order; S and U are privilege modes, not listed. */
    static const char order[] = "IEMAFDQCBKJTPVNH";
    size_t n = 0;
    unsigned i;
    int xlen = (int)(misa >> 62);
    n += (size_t)k_snprintf(buf, size, "rv%d", xlen == 2 ? 64 : xlen == 1 ? 32 : 128);
    for (i = 0; order[i] && n + 1 < size; i++)
        if (misa & (1ULL << (order[i] - 'A')))
            buf[n++] = (char)(order[i] - 'A' + 'a');
    buf[n < size ? n : size - 1] = 0;
    return buf;
}

void arch_report_entry(uint64_t dtb, uint64_t boot_mode)
{
    char isa[40];
    kprintf("[boot] hart 0 entered in %s-mode, SBI shim installed, now in S-mode, "
            "dtb=%p, image %p-%p\n", boot_mode == 3 ? "M" : "?", (void *)dtb,
            __kernel_start, _end);
    kprintf("[boot] ISA %s (misa 0x%lx)\n", isa_string(riscv_boot_misa, isa, sizeof(isa)),
            riscv_boot_misa);
}

void arch_info(void)
{
    char isa[40];
    struct sbiret impl = sbi_call(SBI_EXT_BASE, SBI_BASE_GET_IMPL_ID, 0, 0);
    struct sbiret ver = sbi_call(SBI_EXT_BASE, SBI_BASE_GET_SPEC_VERSION, 0, 0);
    struct sbiret arch = sbi_call(SBI_EXT_BASE, SBI_BASE_GET_MARCHID, 0, 0);
    struct sbiret vend = sbi_call(SBI_EXT_BASE, SBI_BASE_GET_MVENDORID, 0, 0);

    kprintf("plic     : %p, context 1 (hart 0 S-mode), %u sources (%s)\n",
            (void *)plat.plic_base, plic_num_sources(), plat.src_plic);
    kprintf("timer    : SBI set_timer -> CLINT mtimecmp -> STIP, timebase %u Hz (%s)\n",
            plat.timebase_hz, plat.src_timebase);
    kprintf("sbi      : v%ld.%ld, impl 0x%lx%s (TIME, SRST, BASE, legacy)\n",
            (ver.value >> 24) & 0x7f, ver.value & 0xffffff, impl.value,
            impl.value == SBI_IMPL_ORBITOS ? " = OrbitOS built-in M-mode shim" : "");
    kprintf("dtb      : %p (%u bytes)\n", plat.dtb, plat.dtb_size);
    kprintf("cpu      : %s, hart 0, mvendorid=0x%lx marchid=0x%lx\n",
            isa_string(riscv_boot_misa, isa, sizeof(isa)), vend.value, arch.value);
}

void system_reboot(void)
{
    sbi_call(SBI_EXT_SRST, 0, SBI_SRST_COLD_REBOOT, 0);
}

void system_poweroff(void)
{
    sbi_call(SBI_EXT_SRST, 0, SBI_SRST_SHUTDOWN, 0);
}
