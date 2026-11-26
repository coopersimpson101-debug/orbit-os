/* AArch64 helpers: system registers, barriers, MMIO, interrupt masking.
 * The portable kernel includes "arch.h" and gets this file on the virt and
 * rpi5 boards (build.py puts kernel/arch/<arch>/include on the include path). */
#ifndef ORBIT_ARCH_H
#define ORBIT_ARCH_H

#include <stdint.h>

#define ARCH_NAME        "AArch64"
#define ARCH_USER_MODE   "EL0"
#define ARCH_KERNEL_MODE "EL1"

/* Higher-half linear map of RAM installed in TTBR1_EL1. */
#define KERNEL_VA_BASE 0xffff000000000000ULL

#define MMU_BOOT_SUMMARY "identity map + kernel sections at 4K, devices nGnRE"
#define MMU_MODE_DESC    "on (4K granule, 48-bit VA, 4 levels)"
#define POWEROFF_DESC    "PSCI SYSTEM_OFF"
#define REBOOT_DESC      "PSCI SYSTEM_RESET"

#define read_sysreg(r) ({                                   \
    uint64_t __v;                                           \
    __asm__ volatile("mrs %0, " #r : "=r"(__v));            \
    __v; })

#define write_sysreg(r, v) do {                             \
    uint64_t __v = (uint64_t)(v);                           \
    __asm__ volatile("msr " #r ", %0" : : "r"(__v));        \
} while (0)

static inline void isb(void) { __asm__ volatile("isb" ::: "memory"); }
static inline void dsb_sy(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static inline void dsb_ish(void) { __asm__ volatile("dsb ish" ::: "memory"); }
static inline void wfi(void) { __asm__ volatile("wfi" ::: "memory"); }

static inline uint32_t mmio_read32(uint64_t addr)
{
    uint32_t v = *(volatile uint32_t *)(uintptr_t)addr;
    __asm__ volatile("" ::: "memory");
    return v;
}

static inline void mmio_write32(uint64_t addr, uint32_t v)
{
    __asm__ volatile("" ::: "memory");
    *(volatile uint32_t *)(uintptr_t)addr = v;
}

/* DAIF.I masking. irq_save() returns the previous DAIF value. */
static inline uint64_t irq_save(void)
{
    uint64_t f = read_sysreg(daif);
    __asm__ volatile("msr daifset, #2" ::: "memory");
    return f;
}

static inline void irq_restore(uint64_t f)
{
    __asm__ volatile("msr daif, %0" : : "r"(f) : "memory");
}

static inline void irq_enable(void) { __asm__ volatile("msr daifclr, #2" ::: "memory"); }
static inline void irq_disable(void) { __asm__ volatile("msr daifset, #2" ::: "memory"); }

/* EL1 can read EL0 pages directly (PAN is not enabled). */
static inline void user_access_begin(void) { }
static inline void user_access_end(void) { }

static inline uint64_t cpu_counter(void) { isb(); return read_sysreg(cntpct_el0); }
static inline uint64_t cpu_counter_freq(void) { return read_sysreg(cntfrq_el0); }

#endif
