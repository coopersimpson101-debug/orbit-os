/* Refine the compile-time board description from the firmware's DTB.
 * Every value has a hard-coded fallback; DT values are only accepted if
 * they look sane (e.g. MMIO inside the board's device window), because a
 * wrong UART/interrupt-controller address this early means a silent hang.
 *
 * Generic: /memory, the console UART (stdout-path, then a compatible scan).
 * Per architecture: the UART compatible and interrupt encoding, plus
 *   AArch64: GIC, timer PPI, PSCI conduit
 *   RV64:    PLIC, timebase-frequency, machine name */
#include "kernel.h"
#include "fdt.h"

struct platform plat;

static uint32_t be32_at(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int in_window(const struct platform *p, uint64_t a)
{
    return a >= p->dev_base && a < p->dev_base + p->dev_size;
}

#if defined(__aarch64__)
#define UART_COMPAT "arm,pl011"

/* GIC "interrupts" = <type num flags>; type 0 = SPI. */
static int uart_irq_from_dt(const uint8_t *irq, int len)
{
    if (irq && len >= 12 && be32_at(irq) == 0 && be32_at(irq + 4) < 988)
        return (int)(be32_at(irq + 4) + 32);
    return -1;
}

static void probe_gic(struct platform *p, const void *fdt)
{
    static const char *const compat[] = { "arm,gic-400", "arm,cortex-a15-gic",
                                          "arm,cortex-a9-gic" };
    unsigned i;
    for (i = 0; i < ARRAY_SIZE(compat); i++) {
        int n = fdt_node_by_compatible(fdt, -1, compat[i]);
        uint64_t d, c;
        if (n >= 0 && fdt_reg(fdt, n, 0, &d, 0) == 0 && fdt_reg(fdt, n, 1, &c, 0) == 0 &&
            in_window(p, d) && in_window(p, c)) {
            p->gicd_base = d;
            p->gicc_base = c;
            p->src_gic = compat[i];
            return;
        }
    }
    if (fdt_node_by_compatible(fdt, -1, "arm,gic-v3") >= 0)
        kprintf("[fdt] warning: DT describes a GICv3; only GICv2 is supported "
                "(use -M virt,gic-version=2)\n");
}

static void probe_timer_psci(struct platform *p, const void *fdt)
{
    int n = fdt_node_by_compatible(fdt, -1, "arm,armv8-timer"), len;
    const uint8_t *irq;
    if (n >= 0) {
        irq = fdt_getprop(fdt, n, "interrupts", &len);
        if (irq && len >= 24) { /* entry 1 = non-secure EL1 physical timer */
            uint32_t num = be32_at(irq + 16);
            if (num < 16)
                p->timer_irq = (int)(16 + num);
        }
    }
    n = fdt_path_offset(fdt, "/psci");
    if (n < 0)
        n = fdt_node_by_compatible(fdt, -1, "arm,psci-1.0");
    if (n < 0)
        n = fdt_node_by_compatible(fdt, -1, "arm,psci-0.2");
    if (n >= 0) {
        const char *m = fdt_getprop(fdt, n, "method", &len);
        if (m && k_strncmp(m, "smc", 4) == 0) {
            p->psci = PSCI_SMC;
            p->src_psci = "dtb /psci method=smc";
        } else if (m && k_strncmp(m, "hvc", 4) == 0) {
            p->psci = PSCI_HVC;
            p->src_psci = "dtb /psci method=hvc";
        }
    }
}

static void probe_arch(struct platform *p, const void *fdt)
{
    probe_gic(p, fdt);
    probe_timer_psci(p, fdt);
}

#elif defined(__riscv)
#define UART_COMPAT "ns16550a"

/* PLIC "interrupts" = <source>. */
static int uart_irq_from_dt(const uint8_t *irq, int len)
{
    if (irq && len >= 4 && be32_at(irq) > 0 && be32_at(irq) < 1024)
        return (int)be32_at(irq);
    return -1;
}

static void probe_arch(struct platform *p, const void *fdt)
{
    static const char *const plic_compat[] = { "riscv,plic0", "sifive,plic-1.0.0" };
    int root_len, n;
    unsigned i;
    const char *root_compat = fdt_getprop(fdt, 0, "compatible", &root_len);
    uint32_t tb;

    p->machine = root_compat && root_len > 0 && k_strncmp(root_compat, "riscv-virtio", 12) == 0
                 ? "qemu-virt, from DTB" : "unknown, from DTB";
    for (i = 0; i < ARRAY_SIZE(plic_compat); i++) {
        uint64_t a;
        n = fdt_node_by_compatible(fdt, -1, plic_compat[i]);
        if (n >= 0 && fdt_reg(fdt, n, 0, &a, 0) == 0 && in_window(p, a)) {
            p->plic_base = a;
            p->src_plic = plic_compat[i];
            break;
        }
    }
    n = fdt_path_offset(fdt, "/cpus");
    tb = n >= 0 ? fdt_prop_u32(fdt, n, "timebase-frequency", 0) : 0;
    if (tb >= 1000000) {
        p->timebase_hz = tb;
        p->src_timebase = "dtb /cpus timebase-frequency";
    }
}
#endif

static void probe_memory(struct platform *p, const void *fdt)
{
    int node = fdt_path_offset(fdt, "/memory");
    uint64_t a, s;
    if (node >= 0 && fdt_reg(fdt, node, 0, &a, &s) == 0 && s >= (16ULL << 20)) {
        p->ram_base = a;
        p->ram_size = s;
        p->src_mem = "dtb /memory reg[0]";
        if (p->ram_limit && p->ram_size > p->ram_limit) {
            p->ram_size = p->ram_limit;
            p->src_mem = "dtb /memory reg[0], capped by the board";
        }
    }
}

static int pick_uart_node(const void *fdt)
{
    int chosen = fdt_path_offset(fdt, "/chosen"), len, n;
    const char *sp;
    if (chosen >= 0) {
        sp = fdt_getprop(fdt, chosen, "stdout-path", &len);
        if (sp && len > 1) {
            int plen = 0;
            while (plen < len && sp[plen] && sp[plen] != ':')
                plen++;
            n = fdt_resolve(fdt, sp, plen);
            if (n >= 0)
                return n;
        }
    }
    return -1;
}

static int usable_uart(const struct platform *p, const void *fdt, int n, uint64_t *addr)
{
    int len;
    const char *c = fdt_getprop(fdt, n, "compatible", &len);
    int clen = (int)k_strlen(UART_COMPAT);
    if (!c || len < clen || k_strncmp(c, UART_COMPAT, (size_t)clen) != 0 ||
        !fdt_is_enabled(fdt, n))
        return 0;
    return fdt_reg(fdt, n, 0, addr, 0) == 0 && in_window(p, *addr);
}

static void probe_uart(struct platform *p, const void *fdt)
{
    int n = pick_uart_node(fdt), len = 0, first = -1, irq;
    uint64_t a = 0, first_a = 0;
    const char *why = "dtb stdout-path";

    /* Accept stdout-path only if it is a UART we drive inside our device
     * window (on the Pi 5 it may name an RP1 UART behind PCIe, which we
     * cannot reach yet). Otherwise scan, preferring the board's default
     * address. */
    if (n >= 0 && !usable_uart(p, fdt, n, &a))
        n = -1;
    if (n < 0) {
        int it;
        why = "dtb " UART_COMPAT " scan";
        for (it = fdt_node_by_compatible(fdt, -1, UART_COMPAT); it >= 0;
             it = fdt_node_by_compatible(fdt, it, UART_COMPAT)) {
            uint64_t ia;
            if (!usable_uart(p, fdt, it, &ia))
                continue;
            if (first < 0) {
                first = it;
                first_a = ia;
            }
            if (ia == p->uart_base) {
                n = it;
                a = ia;
                break;
            }
        }
        if (n < 0) {
            n = first;
            a = first_a;
        }
    }
    if (n < 0)
        return;
    p->uart_base = a;
    p->src_uart = why;
    irq = uart_irq_from_dt(fdt_getprop(fdt, n, "interrupts", &len), len);
    if (irq >= 0)
        p->uart_irq = irq;
}

void platform_probe_fdt(struct platform *p, uint64_t dtb_addr)
{
    const void *fdt = (const void *)(uintptr_t)dtb_addr;
    if (!dtb_addr || fdt_check(fdt)) {
        kprintf("[fdt] no valid device tree at %p; using %s defaults\n", fdt, p->board);
        return;
    }
    p->dtb = fdt;
    p->dtb_size = fdt_totalsize(fdt);
    kprintf("[fdt] device tree at %p, %u bytes\n", fdt, p->dtb_size);
    probe_memory(p, fdt);
    probe_uart(p, fdt);
    probe_arch(p, fdt);
}
