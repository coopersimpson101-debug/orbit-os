/* orbit-chainloader: linked at CHAIN_BASE (its run address after it
 * relocates itself away from the firmware load address). */
OUTPUT_ARCH(aarch64)
ENTRY(_start)

SECTIONS
{
    . = CHAIN_BASE;
    .text.boot : { KEEP(*(.text.boot)) }
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    . = ALIGN(8);
    __image_copy_end = .;
    .bss (NOLOAD) : {
        . = ALIGN(16);
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(16);
        __bss_end = .;
        . += 0x4000;
        . = ALIGN(16);
        __stack_top = .;
    }
    _end = .;
    _chain_size = _end - _start;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
