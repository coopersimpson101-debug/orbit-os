/*
 * Context switch and thread/user entry trampolines (rv64).
 * struct cpu_ctx layout (context.h): ra, sp, s0..s11  (14 x 8 bytes)
 */
    .section ".text", "ax"

/* void cpu_switch(struct cpu_ctx *prev, struct cpu_ctx *next) */
    .global cpu_switch
cpu_switch:
    sd      ra, 0(a0)
    sd      sp, 8(a0)
    sd      s0, 16(a0)
    sd      s1, 24(a0)
    sd      s2, 32(a0)
    sd      s3, 40(a0)
    sd      s4, 48(a0)
    sd      s5, 56(a0)
    sd      s6, 64(a0)
    sd      s7, 72(a0)
    sd      s8, 80(a0)
    sd      s9, 88(a0)
    sd      s10, 96(a0)
    sd      s11, 104(a0)

    ld      ra, 0(a1)
    ld      sp, 8(a1)
    ld      s0, 16(a1)
    ld      s1, 24(a1)
    ld      s2, 32(a1)
    ld      s3, 40(a1)
    ld      s4, 48(a1)
    ld      s5, 56(a1)
    ld      s6, 64(a1)
    ld      s7, 72(a1)
    ld      s8, 80(a1)
    ld      s9, 88(a1)
    ld      s10, 96(a1)
    ld      s11, 104(a1)
    ret

/* First code a new kernel thread runs: s0 = fn, s1 = arg. We arrive here
 * from schedule() with interrupts masked; threads run with them enabled. */
    .global thread_trampoline
thread_trampoline:
    csrsi   sstatus, 2                  /* SIE */
    mv      a0, s1
    jalr    s0
    li      a0, 0
    call    thread_exit
1:  j       1b

/* enter_user(pc, user_sp, a0, a1, kstack_top): sret to U-mode at pc with
 * an empty kernel stack recorded in sscratch for the next trap. */
    .global enter_user
enter_user:
    csrci   sstatus, 2                  /* no interrupt while sscratch != 0 */
    csrw    sscratch, a4
    csrw    sepc, a0
    li      t0, 0x100                   /* SPP = U */
    csrc    sstatus, t0
    li      t0, 0x20                    /* SPIE: interrupts on in U-mode */
    csrs    sstatus, t0
    mv      sp, a1
    mv      a0, a2
    mv      a1, a3
    li      a2, 0
    li      a3, 0
    li      a4, 0
    li      a5, 0
    li      a6, 0
    li      a7, 0
    li      t0, 0
    li      t1, 0
    li      t2, 0
    li      t3, 0
    li      t4, 0
    li      t5, 0
    li      t6, 0
    li      s0, 0
    li      s1, 0
    li      s2, 0
    li      s3, 0
    li      s4, 0
    li      s5, 0
    li      s6, 0
    li      s7, 0
    li      s8, 0
    li      s9, 0
    li      s10, 0
    li      s11, 0
    li      ra, 0
    li      gp, 0
    li      tp, 0
    sret
