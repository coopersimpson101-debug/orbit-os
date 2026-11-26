/* RV64 helpers for S-mode code: CSRs, fences, MMIO, interrupt masking.
 * The portable kernel includes "arch.h" and gets this file on the rv64
 * board (build.py puts kernel/arch/<arch>/include on the include path). */
#ifndef ORBIT_ARCH_H
#define ORBIT_ARCH_H

#include <stdint.h>

#define ARCH_NAME        "RISC-V"
#define ARCH_USER_MODE   "U"
#define ARCH_KERNEL_MODE "S"

/* Higher-half alias of RAM (Sv39 upper half: VA bits 63..38 all ones). */
#define KERNEL_VA_BASE 0xffffffc000000000ULL

#define MMU_BOOT_SUMMARY "Sv39 identity map, W^X kernel sections at 4K, devices RW"
#define MMU_MODE_DESC    "on (Sv39, 4K pages, 3 levels, A/D pre-set)"
#define POWEROFF_DESC    "SBI SRST shutdown"
#define REBOOT_DESC      "SBI SRST cold reboot"

/* sstatus / sie bits */
#define SSTATUS_SIE  (1UL << 1)
#define SSTATUS_SPIE (1UL << 5)
#define SSTATUS_SPP  (1UL << 8)
#define SSTATUS_SUM  (1UL << 18)
#define SIE_SSIE     (1UL << 1)
#define SIE_STIE     (1UL << 5)
#define SIE_SEIE     (1UL << 9)

#define csr_read(csr) ({                                    \
    uint64_t __v;                                           \
    __asm__ volatile("csrr %0, " #csr : "=r"(__v));         \
    __v; })

#define csr_write(csr, v) do {                              \
    uint64_t __v = (uint64_t)(v);                           \
    __asm__ volatile("csrw " #csr ", %0" : : "r"(__v) : "memory"); \
} while (0)

#define csr_set(csr, bits) do {                             \
    uint64_t __v = (uint64_t)(bits);                        \
    __asm__ volatile("csrs " #csr ", %0" : : "r"(__v) : "memory"); \
} while (0)

#define csr_clear(csr, bits) do {                           \
    uint64_t __v = (uint64_t)(bits);                        \
    __asm__ volatile("csrc " #csr ", %0" : : "r"(__v) : "memory"); \
} while (0)

static inline void wfi(void) { __asm__ volatile("wfi" ::: "memory"); }
static inline void fence(void) { __asm__ volatile("fence rw, rw" ::: "memory"); }
static inline void sfence_vma(void) { __asm__ volatile("sfence.vma zero, zero" ::: "memory"); }

/* Device registers are uncached on orbit-rv and QEMU; the fences keep
 * MMIO ordered against normal memory. */
static inline uint32_t mmio_read32(uint64_t addr)
{
    uint32_t v = *(volatile uint32_t *)(uintptr_t)addr;
    __asm__ volatile("fence i, r" ::: "memory");
    return v;
}

static inline void mmio_write32(uint64_t addr, uint32_t v)
{
    __asm__ volatile("fence w, o" ::: "memory");
    *(volatile uint32_t *)(uintptr_t)addr = v;
}

static inline uint8_t mmio_read8(uint64_t addr)
{
    uint8_t v = *(volatile uint8_t *)(uintptr_t)addr;
    __asm__ volatile("fence i, r" ::: "memory");
    return v;
}

static inline void mmio_write8(uint64_t addr, uint8_t v)
{
    __asm__ volatile("fence w, o" ::: "memory");
    *(volatile uint8_t *)(uintptr_t)addr = v;
}

/* sstatus.SIE masking. irq_save() returns the previous sstatus. */
static inline uint64_t irq_save(void)
{
    uint64_t f;
    __asm__ volatile("csrrci %0, sstatus, 2" : "=r"(f) : : "memory");
    return f;
}

static inline void irq_restore(uint64_t f)
{
    if (f & SSTATUS_SIE)
        __asm__ volatile("csrsi sstatus, 2" ::: "memory");
}

static inline void irq_enable(void) { __asm__ volatile("csrsi sstatus, 2" ::: "memory"); }
static inline void irq_disable(void) { __asm__ volatile("csrci sstatus, 2" ::: "memory"); }

/* S-mode access to U pages (sstatus.SUM), for copying syscall buffers. */
static inline void user_access_begin(void) { csr_set(sstatus, SSTATUS_SUM); }
static inline void user_access_end(void) { csr_clear(sstatus, SSTATUS_SUM); }

/* `time` CSR (CLINT mtime, enabled for S-mode by mcounteren.TM). */
static inline uint64_t cpu_counter(void) { return csr_read(time); }
uint64_t cpu_counter_freq(void); /* timebase, board default or DTB */

#endif
