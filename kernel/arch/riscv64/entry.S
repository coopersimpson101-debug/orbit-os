/*
 * S-mode trap vector (stvec, direct mode) + trap frame save/restore.
 *
 * sscratch convention: 0 while running in S-mode, the top of the current
 * thread's kernel stack while running in U-mode. So a trap from U swaps
 * onto the kernel stack, and a trap from S keeps the current stack.
 * S-mode code never runs with SIE set while sscratch != 0 (enter_user
 * masks interrupts first).
 *
 * Trap frame (struct trapframe in context.h), 288 bytes:
 *   [0..256)  x0..x31 (x0 unused, x2 = interrupted sp)
 *   [256] sstatus  [264] sepc  [272] stval  [280] scause
 */
#define TF_SIZE 288

    .section ".text", "ax"
    .balign 4
    .global trap_entry
trap_entry:
    csrrw   sp, sscratch, sp
    bnez    sp, 1f                      /* from U: sp = kernel stack top */
    csrr    sp, sscratch                /* from S: take sp back */
1:  addi    sp, sp, -TF_SIZE
    sd      x1, 8(sp)
    sd      x3, 24(sp)
    sd      x4, 32(sp)
    sd      x5, 40(sp)
    sd      x6, 48(sp)
    sd      x7, 56(sp)
    sd      x8, 64(sp)
    sd      x9, 72(sp)
    sd      x10, 80(sp)
    sd      x11, 88(sp)
    sd      x12, 96(sp)
    sd      x13, 104(sp)
    sd      x14, 112(sp)
    sd      x15, 120(sp)
    sd      x16, 128(sp)
    sd      x17, 136(sp)
    sd      x18, 144(sp)
    sd      x19, 152(sp)
    sd      x20, 160(sp)
    sd      x21, 168(sp)
    sd      x22, 176(sp)
    sd      x23, 184(sp)
    sd      x24, 192(sp)
    sd      x25, 200(sp)
    sd      x26, 208(sp)
    sd      x27, 216(sp)
    sd      x28, 224(sp)
    sd      x29, 232(sp)
    sd      x30, 240(sp)
    sd      x31, 248(sp)
    csrrw   t0, sscratch, zero          /* interrupted sp; back to "in S" */
    sd      t0, 16(sp)
    csrr    t0, sstatus
    sd      t0, 256(sp)
    csrr    t0, sepc
    sd      t0, 264(sp)
    csrr    t0, stval
    sd      t0, 272(sp)
    csrr    t0, scause
    sd      t0, 280(sp)
    mv      a0, sp
    call    trap_dispatch

    .global ret_from_trap
ret_from_trap:
    ld      t0, 256(sp)
    ld      t1, 264(sp)
    csrw    sstatus, t0                 /* SIE stays 0 until sret */
    csrw    sepc, t1
    andi    t0, t0, 0x100               /* SPP: returning to S? */
    bnez    t0, 2f
    addi    t0, sp, TF_SIZE             /* to U: arm sscratch with our stack top */
    csrw    sscratch, t0
2:  ld      x1, 8(sp)
    ld      x3, 24(sp)
    ld      x4, 32(sp)
    ld      x5, 40(sp)
    ld      x6, 48(sp)
    ld      x7, 56(sp)
    ld      x8, 64(sp)
    ld      x9, 72(sp)
    ld      x10, 80(sp)
    ld      x11, 88(sp)
    ld      x12, 96(sp)
    ld      x13, 104(sp)
    ld      x14, 112(sp)
    ld      x15, 120(sp)
    ld      x16, 128(sp)
    ld      x17, 136(sp)
    ld      x18, 144(sp)
    ld      x19, 152(sp)
    ld      x20, 160(sp)
    ld      x21, 168(sp)
    ld      x22, 176(sp)
    ld      x23, 184(sp)
    ld      x24, 192(sp)
    ld      x25, 200(sp)
    ld      x26, 208(sp)
    ld      x27, 216(sp)
    ld      x28, 224(sp)
    ld      x29, 232(sp)
    ld      x30, 240(sp)
    ld      x31, 248(sp)
    ld      sp, 16(sp)
    sret
