/*
 * OrbitOS rv64 entry point (M-mode).
 *
 * Contract on entry (orbit-rv reset, QEMU virt with -bios none):
 *   hart 0 in M-mode at 0x8000_0000, a0 = hartid, a1 = DTB pointer
 *   (QEMU) or 0 (orbit-rv), satp = 0, interrupts off.
 *
 * Steps:
 *   1. Park every hart except hart 0.
 *   2. Boot stack, BSS clear, record misa for 'info'.
 *   3. Machine trap vector = the SBI shim (sbi_entry below, sbi.c), with
 *      its own stack in mscratch.
 *   4. PMP entry 0 = all of memory RWX for S/U (QEMU enforces PMP;
 *      orbit-rv has none and ignores the writes).
 *   5. Delegate S/U exceptions and the S-level interrupts to S-mode, let
 *      S read time/cycle/instret, arm only the M timer for the shim.
 *   6. mret into S-mode at kernel_main(dtb, 3) with stvec installed.
 */
#define MSTATUS_MPP_MASK (3 << 11)
#define MSTATUS_MPP_S    (1 << 11)
#define MIE_MTIE         (1 << 7)
#define CLINT_MTIMECMP   0x02004000

/* Exceptions handled by the S-mode kernel: misaligned/access faults,
 * illegal instruction, breakpoint, ecall from U, page faults. Not
 * delegated: ecall from S (the SBI) and from M. */
#define MEDELEG_BITS ((1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | \
                      (1 << 5) | (1 << 6) | (1 << 7) | (1 << 8) |             \
                      (1 << 12) | (1 << 13) | (1 << 15))
#define MIDELEG_BITS ((1 << 1) | (1 << 5) | (1 << 9)) /* SSI, STI, SEI */

    .section ".text.boot", "ax"
    .global _start
_start:
    csrw    mie, zero
    csrr    t0, mhartid
    bnez    t0, park

    mv      s0, a1                      /* DTB (QEMU) or 0 */

    /* 2. stack and BSS */
    la      sp, __boot_stack_top
    la      t0, __bss_start
    la      t1, __bss_end
1:  bgeu    t0, t1, 2f
    sd      zero, 0(t0)
    addi    t0, t0, 8
    j       1b
2:
    csrr    t0, misa
    la      t1, riscv_boot_misa
    sd      t0, 0(t1)

    /* 3. machine trap vector + stack */
    la      t0, sbi_entry
    csrw    mtvec, t0
    la      t0, __mstack_top
    csrw    mscratch, t0

    /* No stale timer interrupt: QEMU resets mtimecmp to 0. */
    li      t0, CLINT_MTIMECMP
    li      t1, -1
    sd      t1, 0(t0)

    /* 4. PMP: one NAPOT entry covering everything, RWX */
    li      t0, -1
    srli    t0, t0, 10
    csrw    pmpaddr0, t0
    li      t0, 0x1f
    csrw    pmpcfg0, t0

    /* 5. delegation and counters */
    li      t0, MEDELEG_BITS
    csrw    medeleg, t0
    li      t0, MIDELEG_BITS
    csrw    mideleg, t0
    li      t0, 7                       /* CY, TM, IR */
    csrw    mcounteren, t0
    csrw    mip, zero
    li      t0, MIE_MTIE
    csrw    mie, t0
    csrw    satp, zero

    /* 6. S-mode trap vector, then mret to kernel_main */
    la      t0, trap_entry
    csrw    stvec, t0
    csrw    sscratch, zero
    csrw    sie, zero
    li      t0, MSTATUS_MPP_MASK
    csrc    mstatus, t0
    li      t0, MSTATUS_MPP_S
    csrs    mstatus, t0
    la      t0, s_entry
    csrw    mepc, t0
    mv      a0, s0                      /* dtb */
    li      a1, 3                       /* entered in M-mode */
    mret

s_entry:
    call    kernel_main
park:
    wfi
    j       park

/*
 * Machine trap entry for the SBI shim. mscratch holds the top of the
 * M-mode stack whenever we are not in M-mode (M traps never nest).
 * Frame: x0..x31 (x2 = interrupted sp), 256 bytes; sbi_trap() may modify
 * a0/a1 in it and advances mepc itself for ecalls.
 */
    .balign 4
    .global sbi_entry
sbi_entry:
    csrrw   sp, mscratch, sp
    addi    sp, sp, -256
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
    csrr    t0, mscratch
    sd      t0, 16(sp)
    mv      a0, sp
    call    sbi_trap
    ld      x1, 8(sp)
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
    addi    sp, sp, 256
    csrrw   sp, mscratch, sp
    mret
