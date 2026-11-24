/*
 * OrbitOS entry point.
 *
 * Contract on entry (Raspberry Pi 5 firmware / QEMU "Linux" boot protocol):
 *   x0 = physical address of the DTB, MMU off, D-cache off, any EL (2 on
 *   the Pi 5, 2 on QEMU virt with virtualization=on, 1 otherwise).
 *
 * Steps:
 *   1. Linux arm64 Image header so loaders that understand it (QEMU,
 *      Pi firmware) place us at base+text_offset and pass the DTB in x0.
 *   2. Park every core except the boot core (Aff0..Aff2 == 0). Under PSCI
 *      (Pi 5 TF-A, QEMU) secondaries stay off until CPU_ON so this is a
 *      safety net; under a spin-table they never leave the firmware stub.
 *   3. Self-relocate if we were loaded somewhere other than our link
 *      address (memmove, either direction).
 *   4. EL2 -> EL1 drop (timers, FP and EL1 AArch64 configured), or carry
 *      on at EL1.
 *   5. Stack, BSS, VBAR_EL1, then kernel_main(dtb, boot_el).
 */
#define SCTLR_EL1_RES1  ((3 << 28) | (3 << 22) | (1 << 20) | (1 << 11))
#define HCR_EL2_RW      (1 << 31)
#define SPSR_EL1H_MASKED 0x3c5           /* EL1h, DAIF masked */

    .section ".text.boot", "ax"
    .global _start
_start:
    /* ---- Linux arm64 Image header (Documentation/arch/arm64/booting.rst) ---- */
    b       primary_entry               /* code0 */
    .long   0                           /* code1 */
    .quad   TEXT_OFFSET                 /* text_offset */
    .quad   _kernel_size                /* image_size (incl. BSS) */
    .quad   0x2                         /* flags: LE, 4K pages */
    .quad   0                           /* res2 */
    .quad   0                           /* res3 */
    .quad   0                           /* res4 */
    .long   0x644d5241                  /* "ARM\x64" */
    .long   0                           /* res5 */

primary_entry:
    mov     x19, x0                     /* preserve DTB pointer */

    /* 2. park secondaries (A76 has core id in Aff1, A72 in Aff0) */
    mrs     x1, mpidr_el1
    and     x1, x1, #0xffffff
    cbz     x1, 1f
park:
    wfe
    b       park
1:
    /* 3. self-relocate to the link address if needed */
    adr     x1, _start                  /* where we are */
    ldr     x2, =_start                 /* where we were linked */
    cmp     x1, x2
    b.eq    relocated
    ldr     x3, =__image_copy_end
    sub     x3, x3, x2                  /* bytes to move (8-byte multiple) */
    cmp     x2, x1
    b.hi    copy_backward
copy_forward:                           /* dest below src */
    ldr     x4, [x1], #8
    str     x4, [x2], #8
    subs    x3, x3, #8
    b.gt    copy_forward
    b       copy_done
copy_backward:                          /* dest above src: copy from the end */
    add     x1, x1, x3
    add     x2, x2, x3
2:  ldr     x4, [x1, #-8]!
    str     x4, [x2, #-8]!
    subs    x3, x3, #8
    b.gt    2b
copy_done:
    dsb     sy
    ic      iallu
    dsb     sy
    isb
    ldr     x4, =relocated
    br      x4

relocated:
    /* 4. exception level */
    mrs     x0, CurrentEL
    lsr     x20, x0, #2                 /* x20 = boot EL */
    cmp     x20, #2
    b.eq    from_el2
    cmp     x20, #1
    b.eq    el1_entry
    b       park                        /* EL3 entry is not supported */

from_el2:
    mov     x0, #HCR_EL2_RW             /* EL1 is AArch64, no traps to EL2 */
    msr     hcr_el2, x0
    mrs     x0, cnthctl_el2
    orr     x0, x0, #3                  /* EL1PCTEN | EL1PCEN: EL1 owns the phys timer */
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr
    mrs     x0, midr_el1
    msr     vpidr_el2, x0
    mrs     x0, mpidr_el1
    msr     vmpidr_el2, x0
    mov     x0, #0x33ff                 /* CPTR_EL2: no FP/SIMD traps */
    msr     cptr_el2, x0
    msr     hstr_el2, xzr
    msr     vttbr_el2, xzr
    ldr     x0, =SCTLR_EL1_RES1
    msr     sctlr_el1, x0
    mov     x0, #SPSR_EL1H_MASKED
    msr     spsr_el2, x0
    adr     x0, el1_entry
    msr     elr_el2, x0
    eret

el1_entry:
    ldr     x0, =SCTLR_EL1_RES1         /* MMU + caches off, little endian */
    msr     sctlr_el1, x0
    mov     x0, #(3 << 20)              /* CPACR_EL1.FPEN: no FP traps */
    msr     cpacr_el1, x0
    isb

    /* 5. stack, BSS, vectors */
    ldr     x0, =__boot_stack_top
    mov     sp, x0
    ldr     x1, =__bss_start
    ldr     x2, =__bss_end
3:  cmp     x1, x2
    b.hs    4f
    str     xzr, [x1], #8
    b       3b
4:
    ldr     x0, =vectors
    msr     vbar_el1, x0
    isb

    mov     x0, x19                     /* dtb */
    mov     x1, x20                     /* boot EL */
    bl      kernel_main
    b       park

    .ltorg
