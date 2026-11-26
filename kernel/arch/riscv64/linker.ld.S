/*
 * OrbitOS rv64 kernel linker script. Run through the C preprocessor with
 * -DKERNEL_BASE=<link address> and -DRAM_SIZE=<bytes> (build.py does this).
 *   rv64: KERNEL_BASE 0x80000000 (reset PC), RAM_SIZE 256 KiB
 *
 * Same section groups as the AArch64 script, page aligned so mmu.c can
 * give each its own Sv39 permissions: boot+text RX, .user RX for U-mode,
 * rodata R, data/bss RW, user stacks RW for U-mode. The small-data
 * sections (.sdata/.sbss/.srodata) are folded into their big siblings.
 */
OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
    . = KERNEL_BASE;
    __kernel_start = .;

    .text.boot : { KEEP(*(.text.boot)) }

    . = ALIGN(4096);
    __user_text_start = .;
    .user : {
        *user_prog.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
    }
    . = ALIGN(4096);
    __user_text_end = .;

    .text : { *(.text .text.*) }
    . = ALIGN(4096);
    __text_end = .;

    .rodata : { *(.rodata .rodata.* .srodata .srodata.*) }
    . = ALIGN(4096);
    __rodata_end = .;

    .data : { *(.data .data.* .sdata .sdata.*) }
    . = ALIGN(8);
    __image_copy_end = .;

    .bss (NOLOAD) : {
        . = ALIGN(16);
        __bss_start = .;
        *(.sbss .sbss.* .bss .bss.* COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }

    . = ALIGN(4096);
    __user_stack_start = .;
    .user_stack (NOLOAD) : { *(.user.stack) }
    . = ALIGN(4096);
    __user_stack_end = .;

    /* Idle/boot thread stack (S-mode), then the SBI shim's M-mode stack. */
    .boot_stack (NOLOAD) : {
        . = ALIGN(16);
        . += 0x2000;
        __boot_stack_top = .;
        . += 0x800;
        __mstack_top = .;
    }
    . = ALIGN(4096);
    _end = .;

    _kernel_size = _end - _start;
    ASSERT(_end <= KERNEL_BASE + RAM_SIZE, "kernel image does not fit in RAM")

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
