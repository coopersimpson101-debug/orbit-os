/* C side of the exception vectors (vectors.S). */
#include "arch.h"
#include "drivers.h"
#include "esr.h"
#include "kernel.h"
#include "sched.h"
#include "user.h"

enum vec_type {
    V_EL1T_SYNC = 0, V_EL1T_IRQ, V_EL1T_FIQ, V_EL1T_SERR,
    V_EL1H_SYNC, V_EL1H_IRQ, V_EL1H_FIQ, V_EL1H_SERR,
    V_EL0_SYNC, V_EL0_IRQ, V_EL0_FIQ, V_EL0_SERR,
    V_EL0_32_SYNC, V_EL0_32_IRQ, V_EL0_32_FIQ, V_EL0_32_SERR,
};

static const char *const vec_names[16] = {
    "EL1t sync", "EL1t IRQ", "EL1t FIQ", "EL1t SError",
    "EL1h sync", "EL1h IRQ", "EL1h FIQ", "EL1h SError",
    "EL0 sync",  "EL0 IRQ",  "EL0 FIQ",  "EL0 SError",
    "EL0/A32 sync", "EL0/A32 IRQ", "EL0/A32 FIQ", "EL0/A32 SError",
};

static void report(struct trapframe *tf, unsigned type, uint64_t esr)
{
    char desc[128];
    struct thread *t = current_thread();
    esr_describe(esr, desc, sizeof(desc));
    kprintf("\n*** exception: %s in thread %d (%s)\n", vec_names[type & 15],
            t ? t->tid : -1, t ? t->name : "?");
    kprintf("    ESR_EL1=0x%lx  %s\n", esr, desc);
    kprintf("    ELR_EL1=0x%lx  FAR_EL1=0x%lx  SPSR=0x%lx\n", tf->elr,
            read_sysreg(far_el1), tf->spsr);
}

void exception_dispatch(struct trapframe *tf, uint64_t type)
{
    uint64_t esr;
    struct thread *t = current_thread();

    switch (type) {
    case V_EL1H_IRQ:
    case V_EL0_IRQ:
        irq_dispatch();
        sched_preempt_point();
        return;
    case V_EL0_SYNC:
        esr = read_sysreg(esr_el1);
        if (ESR_EC(esr) == EC_SVC64) {
            syscall_dispatch(tf);
            sched_preempt_point();
            return;
        }
        report(tf, (unsigned)type, esr);
        kprintf("    -> killing user task %d\n", t->tid);
        thread_exit(-1);
    case V_EL1H_SYNC:
        esr = read_sysreg(esr_el1);
        report(tf, (unsigned)type, esr);
        if (t && t->tid > 0) {
            kprintf("    -> killing kernel thread %d, system continues\n", t->tid);
            thread_exit(-1);
        }
        panic("unrecoverable synchronous exception in idle/boot context");
    default:
        esr = read_sysreg(esr_el1);
        report(tf, (unsigned)type, esr);
        panic("unexpected exception vector %lu", type);
    }
}
