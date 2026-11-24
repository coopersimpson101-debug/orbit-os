/*
 * OrbitOS kernel linker script. Run through the C preprocessor with
 * -DKERNEL_BASE=<link address> (build.py / Makefile do this).
 *   rpi5: 0x80000      qemu-virt: 0x40080000
 *
 * Section groups are page aligned so mmu.c can give each its own
 * permissions: boot+text RX, .user RX for EL0, rodata RO, data/bss RW,
 * user stacks RW for EL0.
 */
OUTPUT_ARCH(aarch64)
ENTRY(_start)

SECTIONS
{
    . = KERNEL_BASE;
    __kernel_start = .;

    .text.boot : { KEEP(*(.text.boot)) }

    . = ALIGN(4096);
    __user_text_start = .;
    .user : {
        *user_prog.o(.text .text.* .rodata .rodata.*)
    }
    . = ALIGN(4096);
    __user_text_end = .;

    .text : { *(.text .text.*) }
    . = ALIGN(4096);
    __text_end = .;

    .rodata : { *(.rodata .rodata.*) }
    . = ALIGN(4096);
    __rodata_end = .;

    .data : { *(.data .data.*) }
    . = ALIGN(8);
    __image_copy_end = .;

    .bss (NOLOAD) : {
        . = ALIGN(16);
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }

    . = ALIGN(4096);
    __user_stack_start = .;
    .user_stack (NOLOAD) : { *(.user.stack) }
    . = ALIGN(4096);
    __user_stack_end = .;

    .boot_stack (NOLOAD) : {
        . = ALIGN(16);
        . += 0x4000;
        __boot_stack_top = .;
    }
    . = ALIGN(4096);
    _end = .;

    _kernel_size = _end - _start;

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
