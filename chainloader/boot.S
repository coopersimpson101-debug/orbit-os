/*
 * orbit-chainloader entry. Loaded by the firmware/QEMU exactly where the
 * kernel would be (it carries the same Linux Image header), it copies
 * itself up to CHAIN_BASE so the original load area is free to receive
 * the real kernel over the UART, then calls chain_main(dtb, load_addr).
 * It stays at the entry EL (EL2 on the Pi 5): the kernel does its own
 * EL2->EL1 drop, exactly as if the firmware had started it directly.
 */
    .section ".text.boot", "ax"
    .global _start
_start:
    b       entry
    .long   0
    .quad   TEXT_OFFSET
    .quad   _chain_size
    .quad   0x2
    .quad   0
    .quad   0
    .quad   0
    .long   0x644d5241
    .long   0

entry:
    mov     x19, x0                     /* DTB from firmware */
    mrs     x1, mpidr_el1
    and     x1, x1, #0xffffff
    cbz     x1, 1f
park:
    wfe
    b       park
1:
    adr     x20, _start                 /* load address == future kernel address */
    ldr     x2, =_start
    cmp     x20, x2
    b.eq    relocated                   /* already there (unusual) */
    mov     x1, x20
    ldr     x3, =__image_copy_end
    sub     x3, x3, x2
    cmp     x2, x1
    b.hi    back
fwd:
    ldr     x4, [x1], #8
    str     x4, [x2], #8
    subs    x3, x3, #8
    b.gt    fwd
    b       done
back:
    add     x1, x1, x3
    add     x2, x2, x3
2:  ldr     x4, [x1, #-8]!
    str     x4, [x2, #-8]!
    subs    x3, x3, #8
    b.gt    2b
done:
    dsb     sy
    ic      iallu
    dsb     sy
    isb
    ldr     x4, =relocated
    br      x4

relocated:
    ldr     x0, =__stack_top
    mov     sp, x0
    ldr     x1, =__bss_start
    ldr     x2, =__bss_end
3:  cmp     x1, x2
    b.hs    4f
    str     xzr, [x1], #8
    b       3b
4:
    mov     x0, x19
    mov     x1, x20
    bl      chain_main
    b       park

/* chain_jump(entry, dtb): enter the received kernel like firmware would. */
    .global chain_jump
chain_jump:
    mov     x9, x0
    mov     x0, x1
    mov     x1, xzr
    mov     x2, xzr
    mov     x3, xzr
    dsb     sy
    ic      iallu
    dsb     sy
    isb
    br      x9

    .ltorg
