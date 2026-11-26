/* The OrbitOS SBI shim: a few hundred bytes of M-mode firmware so the
 * kernel can run in S-mode without OpenSBI (docs/sbi.md).
 *
 * Runs only from sbi_entry (boot.S) with mstatus.MIE = 0, on its own
 * stack, with translation off. It owns the CLINT and the finisher; it
 * never touches kernel data structures.
 *
 *   M timer interrupt  -> raise mip.STIP for S-mode, push mtimecmp to max
 *   ecall from S       -> TIME set_timer (and legacy 0x00), SRST, BASE,
 *                         legacy console putchar/getchar
 *   anything else      -> dump mcause/mepc/mtval on the UART and stop */
#include <stdint.h>
#include "kprintf.h"
#include "rv64_map.h"
#include "sbi.h"
#include "scause.h"

#define MIP_STIP (1UL << 5)
#define REG_A0 10
#define REG_A1 11
#define REG_A6 16
#define REG_A7 17

#define mcsr_read(csr) ({                                   \
    uint64_t __v;                                           \
    __asm__ volatile("csrr %0, " #csr : "=r"(__v));         \
    __v; })
#define mcsr_write(csr, v) __asm__ volatile("csrw " #csr ", %0" : : "r"((uint64_t)(v)))
#define mcsr_set(csr, v)   __asm__ volatile("csrs " #csr ", %0" : : "r"((uint64_t)(v)))
#define mcsr_clear(csr, v) __asm__ volatile("csrc " #csr ", %0" : : "r"((uint64_t)(v)))

static volatile uint64_t *const mtimecmp =
    (volatile uint64_t *)(uintptr_t)(RV64_CLINT_BASE + RV64_CLINT_MTIMECMP);

static void m_putc(char c)
{
    volatile uint8_t *uart = (volatile uint8_t *)(uintptr_t)RV64_UART_BASE;
    while (!(uart[5] & 0x20)) /* LSR.THRE */
        ;
    uart[0] = (uint8_t)c;
}

static int m_getc(void)
{
    volatile uint8_t *uart = (volatile uint8_t *)(uintptr_t)RV64_UART_BASE;
    return (uart[5] & 0x01) ? uart[0] : -1;
}

static void m_puts(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            m_putc('\r');
        m_putc(*s);
    }
}

static __attribute__((noreturn)) void finisher(uint32_t cmd)
{
    *(volatile uint32_t *)(uintptr_t)RV64_FINISHER_BASE = cmd;
    for (;;)
        __asm__ volatile("wfi");
}

static void set_timer(uint64_t stime)
{
    *mtimecmp = stime;
    mcsr_clear(mip, MIP_STIP); /* the S-mode timer interrupt is acknowledged */
}

static struct sbiret base_call(long fid, long arg)
{
    struct sbiret r = { SBI_SUCCESS, 0 };
    switch (fid) {
    case SBI_BASE_GET_SPEC_VERSION: r.value = SBI_SPEC_VERSION; break;
    case SBI_BASE_GET_IMPL_ID:      r.value = SBI_IMPL_ORBITOS; break;
    case SBI_BASE_GET_IMPL_VERSION: r.value = SBI_IMPL_VERSION; break;
    case SBI_BASE_PROBE_EXT:
        r.value = arg == SBI_EXT_BASE || arg == SBI_EXT_TIME || arg == SBI_EXT_SRST ||
                  arg == SBI_EXT_LEGACY_SET_TIMER || arg == SBI_EXT_LEGACY_PUTCHAR ||
                  arg == SBI_EXT_LEGACY_GETCHAR;
        break;
    case SBI_BASE_GET_MVENDORID: r.value = (long)mcsr_read(mvendorid); break;
    case SBI_BASE_GET_MARCHID:   r.value = (long)mcsr_read(marchid); break;
    case SBI_BASE_GET_MIMPID:    r.value = (long)mcsr_read(mimpid); break;
    default:                     r.error = SBI_ERR_NOT_SUPPORTED; break;
    }
    return r;
}

static struct sbiret srst_call(long fid, long type)
{
    struct sbiret r = { SBI_ERR_NOT_SUPPORTED, 0 };
    if (fid != 0)
        return r;
    if (type == SBI_SRST_SHUTDOWN)
        finisher(FINISHER_PASS);
    if (type == SBI_SRST_COLD_REBOOT || type == SBI_SRST_WARM_REBOOT)
        finisher(FINISHER_RESET);
    r.error = SBI_ERR_INVALID_PARAM;
    return r;
}

static void ecall_from_s(uint64_t *regs)
{
    long ext = (long)regs[REG_A7], fid = (long)regs[REG_A6];
    struct sbiret r = { SBI_ERR_NOT_SUPPORTED, 0 };
    switch (ext) {
    case SBI_EXT_TIME:
        if (fid == 0) {
            set_timer(regs[REG_A0]);
            r.error = SBI_SUCCESS;
        }
        break;
    case SBI_EXT_LEGACY_SET_TIMER: /* legacy calls return only a0 */
        set_timer(regs[REG_A0]);
        regs[REG_A0] = 0;
        return;
    case SBI_EXT_LEGACY_PUTCHAR:
        m_putc((char)regs[REG_A0]);
        regs[REG_A0] = 0;
        return;
    case SBI_EXT_LEGACY_GETCHAR:
        regs[REG_A0] = (uint64_t)(long)m_getc();
        return;
    case SBI_EXT_BASE:
        r = base_call(fid, (long)regs[REG_A0]);
        break;
    case SBI_EXT_SRST:
        r = srst_call(fid, (long)regs[REG_A0]);
        break;
    default:
        break;
    }
    regs[REG_A0] = (uint64_t)r.error;
    regs[REG_A1] = (uint64_t)r.value;
}

static __attribute__((noreturn)) void unexpected(uint64_t cause)
{
    char buf[160];
    k_snprintf(buf, sizeof(buf),
               "\n*** SBI shim: unexpected M-mode trap: %s\n"
               "    mcause=0x%lx mepc=0x%lx mtval=0x%lx mstatus=0x%lx\n",
               scause_name(cause), cause, mcsr_read(mepc), mcsr_read(mtval),
               mcsr_read(mstatus));
    m_puts(buf);
    finisher(FINISHER_FAIL | (1u << 16));
}

void sbi_trap(uint64_t *regs)
{
    uint64_t cause = mcsr_read(mcause);
    if (cause == (CAUSE_IRQ_FLAG | IRQ_M_TIMER)) {
        *mtimecmp = ~0ULL;          /* one shot: S re-arms via set_timer */
        mcsr_set(mip, MIP_STIP);
        return;
    }
    if (cause == EXC_ECALL_S) {
        mcsr_write(mepc, mcsr_read(mepc) + 4);
        ecall_from_s(regs);
        return;
    }
    unexpected(cause);
}
