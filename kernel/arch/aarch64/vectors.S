/*
 * EL1 exception vector table + trap frame save/restore.
 *
 * Trap frame (struct trapframe in user.h), 272 bytes, on the current
 * thread's kernel stack (SP_EL1):
 *   [0..240)  x0..x29      [240] x30   [248] sp_el0
 *   [256]     elr_el1      [264] spsr_el1
 */
#define TF_SIZE 272

.macro kernel_entry
    sub     sp, sp, #TF_SIZE
    stp     x0, x1, [sp, #16 * 0]
    stp     x2, x3, [sp, #16 * 1]
    stp     x4, x5, [sp, #16 * 2]
    stp     x6, x7, [sp, #16 * 3]
    stp     x8, x9, [sp, #16 * 4]
    stp     x10, x11, [sp, #16 * 5]
    stp     x12, x13, [sp, #16 * 6]
    stp     x14, x15, [sp, #16 * 7]
    stp     x16, x17, [sp, #16 * 8]
    stp     x18, x19, [sp, #16 * 9]
    stp     x20, x21, [sp, #16 * 10]
    stp     x22, x23, [sp, #16 * 11]
    stp     x24, x25, [sp, #16 * 12]
    stp     x26, x27, [sp, #16 * 13]
    stp     x28, x29, [sp, #16 * 14]
    mrs     x21, sp_el0
    stp     x30, x21, [sp, #240]
    mrs     x22, elr_el1
    mrs     x23, spsr_el1
    stp     x22, x23, [sp, #256]
.endm

.macro kernel_exit
    ldp     x22, x23, [sp, #256]
    msr     elr_el1, x22
    msr     spsr_el1, x23
    ldp     x30, x21, [sp, #240]
    msr     sp_el0, x21
    ldp     x0, x1, [sp, #16 * 0]
    ldp     x2, x3, [sp, #16 * 1]
    ldp     x4, x5, [sp, #16 * 2]
    ldp     x6, x7, [sp, #16 * 3]
    ldp     x8, x9, [sp, #16 * 4]
    ldp     x10, x11, [sp, #16 * 5]
    ldp     x12, x13, [sp, #16 * 6]
    ldp     x14, x15, [sp, #16 * 7]
    ldp     x16, x17, [sp, #16 * 8]
    ldp     x18, x19, [sp, #16 * 9]
    ldp     x20, x21, [sp, #16 * 10]
    ldp     x22, x23, [sp, #16 * 11]
    ldp     x24, x25, [sp, #16 * 12]
    ldp     x26, x27, [sp, #16 * 13]
    ldp     x28, x29, [sp, #16 * 14]
    add     sp, sp, #TF_SIZE
    eret
.endm

.macro ventry label
    .balign 0x80
    b       \label
.endm

.macro vhandler name, type
\name:
    kernel_entry
    mov     x0, sp
    mov     x1, #\type
    bl      exception_dispatch
    b       ret_from_exception
.endm

    .section ".text", "ax"
    .balign 0x800
    .global vectors
vectors:
    /* current EL, SP_EL0 */
    ventry  v_el1t_sync
    ventry  v_el1t_irq
    ventry  v_el1t_fiq
    ventry  v_el1t_serr
    /* current EL, SP_ELx */
    ventry  v_el1h_sync
    ventry  v_el1h_irq
    ventry  v_el1h_fiq
    ventry  v_el1h_serr
    /* lower EL, AArch64 */
    ventry  v_el0_sync
    ventry  v_el0_irq
    ventry  v_el0_fiq
    ventry  v_el0_serr
    /* lower EL, AArch32 */
    ventry  v_el0_32_sync
    ventry  v_el0_32_irq
    ventry  v_el0_32_fiq
    ventry  v_el0_32_serr

    .balign 4
    vhandler v_el1t_sync, 0
    vhandler v_el1t_irq, 1
    vhandler v_el1t_fiq, 2
    vhandler v_el1t_serr, 3
    vhandler v_el1h_sync, 4
    vhandler v_el1h_irq, 5
    vhandler v_el1h_fiq, 6
    vhandler v_el1h_serr, 7
    vhandler v_el0_sync, 8
    vhandler v_el0_irq, 9
    vhandler v_el0_fiq, 10
    vhandler v_el0_serr, 11
    vhandler v_el0_32_sync, 12
    vhandler v_el0_32_irq, 13
    vhandler v_el0_32_fiq, 14
    vhandler v_el0_32_serr, 15

    .global ret_from_exception
ret_from_exception:
    kernel_exit
