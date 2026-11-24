/*
 * Context switch and thread/user entry trampolines.
 * struct cpu_ctx layout (sched.h): x19..x28, fp, lr, sp  (13 x 8 bytes)
 */
    .section ".text", "ax"

/* void cpu_switch(struct cpu_ctx *prev, struct cpu_ctx *next) */
    .global cpu_switch
cpu_switch:
    stp     x19, x20, [x0, #0]
    stp     x21, x22, [x0, #16]
    stp     x23, x24, [x0, #32]
    stp     x25, x26, [x0, #48]
    stp     x27, x28, [x0, #64]
    stp     x29, x30, [x0, #80]
    mov     x9, sp
    str     x9, [x0, #96]

    ldp     x19, x20, [x1, #0]
    ldp     x21, x22, [x1, #16]
    ldp     x23, x24, [x1, #32]
    ldp     x25, x26, [x1, #48]
    ldp     x27, x28, [x1, #64]
    ldp     x29, x30, [x1, #80]
    ldr     x9, [x1, #96]
    mov     sp, x9
    ret

/* First code a new kernel thread runs: x19 = fn, x20 = arg. We arrive
 * here from schedule() with IRQs masked; threads run with them enabled. */
    .global thread_trampoline
thread_trampoline:
    msr     daifclr, #2
    mov     x0, x20
    blr     x19
    mov     x0, #0
    bl      thread_exit
1:  b       1b

/* enter_user(pc, user_sp, a0, a1, kstack_top) */
    .global enter_user
enter_user:
    msr     daifset, #2                 /* no IRQ between ELR/SPSR setup and eret */
    mov     sp, x4
    msr     sp_el0, x1
    msr     elr_el1, x0
    msr     spsr_el1, xzr               /* EL0t, all exceptions unmasked */
    mov     x0, x2
    mov     x1, x3
    mov     x2, xzr
    mov     x3, xzr
    mov     x4, xzr
    mov     x5, xzr
    mov     x6, xzr
    mov     x7, xzr
    mov     x8, xzr
    mov     x9, xzr
    mov     x10, xzr
    mov     x11, xzr
    mov     x12, xzr
    mov     x13, xzr
    mov     x14, xzr
    mov     x15, xzr
    mov     x16, xzr
    mov     x17, xzr
    mov     x18, xzr
    mov     x19, xzr
    mov     x20, xzr
    mov     x21, xzr
    mov     x22, xzr
    mov     x23, xzr
    mov     x24, xzr
    mov     x25, xzr
    mov     x26, xzr
    mov     x27, xzr
    mov     x28, xzr
    mov     x29, xzr
    mov     x30, xzr
    eret
