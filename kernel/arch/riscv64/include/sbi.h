/* Supervisor Binary Interface: the S-mode caller side, and the constants
 * shared with the built-in M-mode shim (sbi.c). See docs/sbi.md. */
#ifndef ORBIT_SBI_H
#define ORBIT_SBI_H

#include <stdint.h>

/* Extension IDs (a7) */
#define SBI_EXT_LEGACY_SET_TIMER 0x00
#define SBI_EXT_LEGACY_PUTCHAR   0x01
#define SBI_EXT_LEGACY_GETCHAR   0x02
#define SBI_EXT_BASE             0x10
#define SBI_EXT_TIME             0x54494D45 /* "TIME" */
#define SBI_EXT_SRST             0x53525354 /* "SRST" */

/* BASE function IDs (a6) */
#define SBI_BASE_GET_SPEC_VERSION 0
#define SBI_BASE_GET_IMPL_ID      1
#define SBI_BASE_GET_IMPL_VERSION 2
#define SBI_BASE_PROBE_EXT        3
#define SBI_BASE_GET_MVENDORID    4
#define SBI_BASE_GET_MARCHID      5
#define SBI_BASE_GET_MIMPID       6

#define SBI_SRST_SHUTDOWN    0
#define SBI_SRST_COLD_REBOOT 1
#define SBI_SRST_WARM_REBOOT 2

#define SBI_SUCCESS            0
#define SBI_ERR_FAILED        -1
#define SBI_ERR_NOT_SUPPORTED -2
#define SBI_ERR_INVALID_PARAM -3

/* Implementation ID for the OrbitOS shim (0..5 are taken by BBL, OpenSBI,
 * Xvisor, KVM, RustSBI, Diosix). Experimental value, not registered. */
#define SBI_IMPL_ORBITOS      0x4f524254 /* "ORBT" */
#define SBI_IMPL_VERSION      0x00000100 /* 0.1.0 */
#define SBI_SPEC_VERSION      ((2u << 24) | 0u) /* SBI v2.0 subset */

struct sbiret {
    long error;
    long value;
};

static inline struct sbiret sbi_call(long ext, long fid, long a0, long a1)
{
    register long r0 __asm__("a0") = a0;
    register long r1 __asm__("a1") = a1;
    register long r6 __asm__("a6") = fid;
    register long r7 __asm__("a7") = ext;
    struct sbiret ret;
    __asm__ volatile("ecall" : "+r"(r0), "+r"(r1) : "r"(r6), "r"(r7) : "memory");
    ret.error = r0;
    ret.value = r1;
    return ret;
}

static inline void sbi_set_timer(uint64_t stime)
{
    sbi_call(SBI_EXT_TIME, 0, (long)stime, 0);
}

/* M-mode shim entry, called from sbi_entry in boot.S with the saved
 * registers x0..x31. */
void sbi_trap(uint64_t *regs);

#endif
