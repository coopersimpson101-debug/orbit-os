/* GICv2 / GIC-400 driver (distributor + CPU interface, single core).
 * Pi 5: GIC-400 inside the BCM2712. QEMU virt: -M virt,gic-version=2. */
#include "arch.h"
#include "drivers.h"
#include "kernel.h"

#define GICD_CTLR       0x000
#define GICD_TYPER      0x004
#define GICD_ISENABLER  0x100
#define GICD_ICENABLER  0x180
#define GICD_ICPENDR    0x280
#define GICD_IPRIORITYR 0x400
#define GICD_ITARGETSR  0x800
#define GICD_ICFGR      0xc00

#define GICC_CTLR 0x000
#define GICC_PMR  0x004
#define GICC_BPR  0x008
#define GICC_IAR  0x00c
#define GICC_EOIR 0x010

static uint64_t gicd, gicc;
static uint32_t nlines;

static void set_byte_field(uint64_t reg_base, int intid, uint8_t val)
{
    uint64_t reg = reg_base + (uint64_t)(intid & ~3);
    int shift = (intid & 3) * 8;
    uint32_t v = mmio_read32(reg);
    v &= ~(0xffu << shift);
    v |= (uint32_t)val << shift;
    mmio_write32(reg, v);
}

void gic_init(uint64_t d, uint64_t c)
{
    uint32_t i;
    gicd = d;
    gicc = c;
    mmio_write32(gicd + GICD_CTLR, 0);
    nlines = 32 * ((mmio_read32(gicd + GICD_TYPER) & 0x1f) + 1);
    if (nlines > 1020)
        nlines = 1020;
    for (i = 0; i < nlines; i += 32) {
        mmio_write32(gicd + GICD_ICENABLER + i / 8, 0xffffffffu);
        mmio_write32(gicd + GICD_ICPENDR + i / 8, 0xffffffffu);
    }
    for (i = 32; i < nlines; i += 4) {
        mmio_write32(gicd + GICD_IPRIORITYR + i, 0xa0a0a0a0u);
        mmio_write32(gicd + GICD_ITARGETSR + i, 0x01010101u); /* CPU0 */
    }
    for (i = 32; i < nlines; i += 16)
        mmio_write32(gicd + GICD_ICFGR + i / 4, 0); /* level-sensitive */
    for (i = 0; i < 32; i += 4)
        mmio_write32(gicd + GICD_IPRIORITYR + i, 0xa0a0a0a0u);
    mmio_write32(gicd + GICD_CTLR, 1);

    mmio_write32(gicc + GICC_PMR, 0xf0);
    mmio_write32(gicc + GICC_BPR, 0);
    mmio_write32(gicc + GICC_CTLR, 1);
}

void gic_enable_irq(int intid)
{
    if (intid < 0 || (uint32_t)intid >= nlines)
        return;
    set_byte_field(gicd + GICD_IPRIORITYR, intid, 0xa0);
    if (intid >= 32)
        set_byte_field(gicd + GICD_ITARGETSR, intid, 0x01);
    mmio_write32(gicd + GICD_ISENABLER + 4 * (uint64_t)(intid / 32),
                 1u << (intid % 32));
}

uint32_t gic_ack(void)
{
    return mmio_read32(gicc + GICC_IAR);
}

void gic_eoi(uint32_t iar)
{
    mmio_write32(gicc + GICC_EOIR, iar);
}

uint32_t gic_num_lines(void)
{
    return nlines;
}

void irqchip_init(void)
{
    gic_init(plat.gicd_base, plat.gicc_base);
    kprintf("[gic] GICv2 at D=%p C=%p, %u interrupt lines\n", (void *)plat.gicd_base,
            (void *)plat.gicc_base, gic_num_lines());
}

void irqchip_enable(int irq)
{
    gic_enable_irq(irq);
}
