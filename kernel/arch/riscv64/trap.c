/* C side of the S-mode trap vector (entry.S): interrupts, system calls,
 * and fault isolation. The RV64 counterpart of arch/aarch64/trap.c. */
#include "arch.h"
#include "drivers.h"
#include "kernel.h"
#include "scause.h"
#include "sched.h"
#include "user.h"

static void report(const struct trapframe *tf, int from_user)
{
    char desc[128];
    struct thread *t = current_thread();
    scause_describe(tf->scause, tf->stval, from_user, desc, sizeof(desc));
    kprintf("\n*** exception: %s in thread %d (%s)\n", scause_name(tf->scause),
            t ? t->tid : -1, t ? t->name : "?");
    kprintf("    %s\n", desc);
    kprintf("    sepc=0x%lx  stval=0x%lx  sstatus=0x%lx\n", tf->sepc, tf->stval,
            tf->sstatus);
}

static void interrupt(uint64_t code)
{
    switch (code) {
    case IRQ_S_TIMER:
        timer_irq();
        break;
    case IRQ_S_EXT:
        plic_irq();
        break;
    case IRQ_S_SOFT:
        csr_clear(sip, SIE_SSIE);
        break;
    default:
        kprintf("[irq] unexpected interrupt %lu\n", code);
        break;
    }
}

void trap_dispatch(struct trapframe *tf)
{
    uint64_t cause = tf->scause;
    int from_user = !(tf->sstatus & SSTATUS_SPP);
    struct thread *t = current_thread();

    if (CAUSE_IS_IRQ(cause)) {
        interrupt(CAUSE_CODE(cause));
        sched_preempt_point();
        return;
    }
    if (from_user) {
        if (cause == EXC_ECALL_U) {
            tf->sepc += 4; /* resume after the ecall */
            syscall_dispatch(tf);
            sched_preempt_point();
            return;
        }
        report(tf, 1);
        kprintf("    -> killing user task %d\n", t->tid);
        thread_exit(-1);
    }
    report(tf, 0);
    if (t && t->tid > 0) {
        kprintf("    -> killing kernel thread %d, system continues\n", t->tid);
        thread_exit(-1);
    }
    panic("unrecoverable exception in idle/boot context");
}
