/* UART shell: runs as a priority-1 kernel thread. */
#include "arch.h"
#include "drivers.h"
#include "hal.h"
#include "kernel.h"
#include "mm.h"
#include "sched.h"
#include "shell_parse.h"
#include "demos.h"

#define PROMPT "orbit> "

struct cmd {
    const char *name;
    const char *help;
    void (*fn)(int argc, char **argv);
};

static const struct cmd *find_cmd(const char *name);

static void shell_echo(void *ctx, const char *s, size_t n)
{
    (void)ctx;
    console_write(s, n);
}

/* ---- commands ---------------------------------------------------------- */

static void cmd_help(int argc, char **argv);

static void cmd_echo(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++)
        kprintf("%s%s", argv[i], i + 1 < argc ? " " : "");
    kputs("\n");
}

static void cmd_uptime(int argc, char **argv)
{
    uint64_t ms = uptime_ms();
    (void)argc;
    (void)argv;
    kprintf("up %lu.%03lu s (%lu ticks at %d Hz, %lu context switches)\n", ms / 1000,
            ms % 1000, timer_ticks(), HZ, sched_context_switches());
}

static void cmd_ticks(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    kprintf("%lu\n", timer_ticks());
}

static void cmd_mem(int argc, char **argv)
{
    const struct buddy *b = mm_buddy();
    const struct kheap_stats *h = mm_heap_stats();
    unsigned o;
    (void)argc;
    (void)argv;
    int mib = plat.ram_size >= (1ULL << 20);
    kprintf("RAM      : %p - %p (%lu %s, %s)\n", (void *)plat.ram_base,
            (void *)(plat.ram_base + plat.ram_size),
            mib ? plat.ram_size >> 20 : plat.ram_size >> 10, mib ? "MiB" : "KiB",
            plat.src_mem);
    kprintf("kernel   : %p - %p (%lu KiB)  text..%p rodata..%p\n", __kernel_start, _end,
            (uint64_t)(_end - __kernel_start) >> 10, __text_end, __rodata_end);
    kprintf("user sec : text %p - %p, stacks %p - %p\n", __user_text_start,
            __user_text_end, __user_stack_start, __user_stack_end);
    kprintf("pages    : %lu free / %lu managed (4 KiB)\n", (uint64_t)buddy_free_pages(b),
            (uint64_t)b->managed_pages);
    kputs("free blks:");
    for (o = 0; o < BUDDY_ORDERS; o++)
        kprintf(" o%u:%lu", o, (uint64_t)b->nfree[o]);
    kputs("\n");
    kprintf("heap     : %lu B in use, %lu slab pages, %lu large pages, %lu allocs, "
            "%lu frees, %lu failed\n", (uint64_t)h->bytes_in_use, (uint64_t)h->slab_pages,
            (uint64_t)h->large_pages, (uint64_t)h->allocs, (uint64_t)h->frees,
            (uint64_t)h->failed);
    kprintf("mmu      : %s, %lu page tables, higher-half alias at %p\n",
            mmu_enabled() ? MMU_MODE_DESC : "off",
            (uint64_t)mmu_tables_used(), (void *)KERNEL_VA_BASE);
}

static void cmd_ps(int argc, char **argv)
{
    int i;
    struct thread *me = current_thread();
    (void)argc;
    (void)argv;
    kputs("TID  NAME             PRIO  STATE     MODE  TICKS\n");
    for (i = 0; i < MAX_THREADS; i++) {
        struct thread *t = thread_at(i);
        if (t->state == T_UNUSED)
            continue;
        kprintf("%-4d %-16s %-5d %-9s %-5s %lu%s\n", t->tid, t->name, t->prio,
                thread_state_name(t->state), t->is_user ? ARCH_USER_MODE : ARCH_KERNEL_MODE,
                t->cpu_ticks,
                t == me ? "  <- you" : "");
    }
}

static void cmd_info(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (plat.machine)
        kprintf("board    : %s (%s)\n", plat.board, plat.machine);
    else
        kprintf("board    : %s\n", plat.board);
    kprintf("arch     : %s\n", ARCH_NAME);
    kprintf("uart     : %s @ %p, %s, %s\n", uart_name(), (void *)plat.uart_base,
            uart_rx_irq_mode() ? "RX IRQ" : "polled RX", plat.src_uart);
    arch_info();
}

/* peek/poke are guarded: 4-byte aligned, inside mapped RAM or the device
 * window, and poke refuses the kernel image and page tables. */
static int addr_ok(uint64_t a, int for_write, const char **why)
{
    int in_ram = a >= plat.ram_base && a + 4 <= plat.ram_base + plat.ram_size &&
                 a + 4 <= plat.ram_base + (4ULL << 30);
    int in_dev = a >= plat.dev_base && a + 4 <= plat.dev_base + plat.dev_size;
    if (a & 3) {
        *why = "address must be 4-byte aligned";
        return 0;
    }
    if (!in_ram && !in_dev) {
        *why = "address not in mapped RAM or the device window";
        return 0;
    }
    if (for_write && a >= (uint64_t)__kernel_start && a < (uint64_t)_end) {
        *why = "refusing to poke the kernel image";
        return 0;
    }
    if (mmu_enabled() && mmu_translate(a) == ~0ULL) {
        *why = "address is not mapped";
        return 0;
    }
    return 1;
}

static void cmd_peek(int argc, char **argv)
{
    uint64_t a, n = 1, i;
    const char *why;
    if (argc < 2 || parse_u64(argv[1], &a) || (argc > 2 && parse_u64(argv[2], &n))) {
        kputs("usage: peek <addr> [count<=64]\n");
        return;
    }
    if (n > 64)
        n = 64;
    for (i = 0; i < n; i++) {
        uint64_t x = a + 4 * i;
        if (!addr_ok(x, 0, &why)) {
            kprintf("peek: %s\n", why);
            return;
        }
        if (i % 4 == 0)
            kprintf("%s%p:", i ? "\n" : "", (void *)x);
        kprintf(" %08x", mmio_read32(x));
    }
    kputs("\n");
}

static void cmd_poke(int argc, char **argv)
{
    uint64_t a, v;
    const char *why;
    if (argc != 3 || parse_u64(argv[1], &a) || parse_u64(argv[2], &v) || v > 0xffffffffULL) {
        kputs("usage: poke <addr> <u32 value>\n");
        return;
    }
    if (!addr_ok(a, 1, &why)) {
        kprintf("poke: %s\n", why);
        return;
    }
    mmio_write32(a, (uint32_t)v);
    kprintf("%p <- %08x (reads back %08x)\n", (void *)a, (uint32_t)v, mmio_read32(a));
}

static void cmd_spawn(int argc, char **argv)
{
    int r;
    if (argc != 2) {
        kprintf("usage: spawn <%s>\n", demo_names());
        return;
    }
    r = demo_spawn(argv[1]);
    if (r == -2)
        kprintf("spawn: unknown demo '%s' (try: %s)\n", argv[1], demo_names());
    else if (r < 0)
        kputs("spawn: out of threads/stack slots\n");
}

static void cmd_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    kputs("rebooting via " REBOOT_DESC "...\n");
    system_reboot();
    kputs("reboot: firmware call returned - not supported on this platform\n");
}

static void cmd_poweroff(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    kputs("powering off via " POWEROFF_DESC "...\n");
    system_poweroff();
    kputs("poweroff: firmware call returned - not supported on this platform\n");
}

static const struct cmd cmds[] = {
    { "help",     "list commands",                         cmd_help },
    { "info",     "board/platform details from DTB",       cmd_info },
    { "mem",      "memory map, page allocator, heap stats", cmd_mem },
    { "ps",       "list threads",                          cmd_ps },
    { "uptime",   "time since boot",                       cmd_uptime },
    { "ticks",    "timer interrupts since boot",           cmd_ticks },
    { "echo",     "echo arguments",                        cmd_echo },
    { "peek",     "peek <addr> [n]: read 32-bit words",    cmd_peek },
    { "poke",     "poke <addr> <val>: write a 32-bit word", cmd_poke },
    { "spawn",    "spawn <demo>: start a demo task",       cmd_spawn },
    { "reboot",   REBOOT_DESC,                             cmd_reboot },
    { "poweroff", POWEROFF_DESC,                           cmd_poweroff },
};

static void cmd_help(int argc, char **argv)
{
    unsigned i;
    (void)argc;
    (void)argv;
    for (i = 0; i < ARRAY_SIZE(cmds); i++)
        kprintf("  %-9s %s\n", cmds[i].name, cmds[i].help);
    kprintf("  demos: %s\n", demo_names());
}

static const struct cmd *find_cmd(const char *name)
{
    unsigned i;
    for (i = 0; i < ARRAY_SIZE(cmds); i++)
        if (!k_strcmp(cmds[i].name, name))
            return &cmds[i];
    return 0;
}

void shell_thread(void *arg)
{
    struct line_editor le;
    (void)arg;
    le_init(&le, shell_echo, 0);
    kputs("\nOrbitOS shell - type 'help'\n" PROMPT);
    for (;;) {
        char c = uart_getc_blocking();
        enum line_result r = le_feed(&le, c);
        if (r == LINE_READY) {
            char *argv[SHELL_MAX_ARGS];
            int argc = shell_tokenize(le.buf, argv, SHELL_MAX_ARGS);
            if (argc < 0) {
                kputs("parse error (unbalanced quotes or too many args)\n");
            } else if (argc > 0) {
                const struct cmd *cmd = find_cmd(argv[0]);
                if (cmd)
                    cmd->fn(argc, argv);
                else
                    kprintf("unknown command '%s' - try 'help'\n", argv[0]);
            }
            kputs(PROMPT);
        } else if (r == LINE_CANCELLED) {
            kputs(PROMPT);
        }
    }
}
