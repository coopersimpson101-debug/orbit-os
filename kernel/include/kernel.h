/* Kernel-wide declarations. */
#ifndef ORBIT_KERNEL_H
#define ORBIT_KERNEL_H

#include <stddef.h>
#include <stdint.h>
#include "kstring.h"
#include "kprintf.h"

#define ORBIT_VERSION "0.2.0"

#define ALIGN_UP(x, a)   (((x) + ((a) - 1)) & ~((uint64_t)(a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((uint64_t)(a) - 1))
#define ARRAY_SIZE(a)    (sizeof(a) / sizeof((a)[0]))

void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void kputs(const char *s);
void console_write(const char *s, size_t n);
__attribute__((noreturn)) void panic(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

/* Platform description: compile-time board defaults, refined from the DTB. */
enum psci_conduit { PSCI_NONE = 0, PSCI_SMC, PSCI_HVC };

struct platform {
    const char *board;
    const char *machine;   /* what the DTB says we run on, NULL if unknown */
    uint64_t ram_base, ram_size;
    uint64_t ram_limit;    /* board cap on usable RAM, 0 = take the DTB's size */
    uint64_t uart_base;
    uint32_t uart_clock;   /* 0 = leave the firmware's baud setup alone */
    int uart_irq;          /* GIC INTID / PLIC source, -1 = poll */
#if defined(__aarch64__)
    uint64_t gicd_base, gicc_base;
    int timer_irq;         /* NS EL1 physical timer PPI INTID */
    enum psci_conduit psci;
#elif defined(__riscv)
    uint64_t plic_base;
    uint32_t timebase_hz;  /* mtime / time CSR frequency */
#endif
    uint64_t dev_base, dev_size; /* MMIO window (Device-nGnRE / Sv39 RW, no X) */
    const void *dtb;
    uint32_t dtb_size;
    /* Provenance strings for the 'info' command. */
    const char *src_mem, *src_uart;
#if defined(__aarch64__)
    const char *src_gic, *src_psci;
#elif defined(__riscv)
    const char *src_plic, *src_timebase;
#endif
};

extern struct platform plat;

void board_defaults(struct platform *p, int boot_el); /* board_<name>.c */
void platform_probe_fdt(struct platform *p, uint64_t dtb_addr);

/* Linker-script symbols. */
extern char __kernel_start[], __user_text_start[], __user_text_end[];
extern char __text_end[], __rodata_end[], __bss_start[], __bss_end[];
extern char __user_stack_start[], __user_stack_end[], _end[];

#endif
