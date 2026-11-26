/* RISC-V Platform-Level Interrupt Controller, hart 0 S-mode context
 * (context 1 on orbit-rv and QEMU virt). All registers are 32 bits. */
#include "arch.h"
#include "drivers.h"
#include "kernel.h"

#define PLIC_PRIORITY(src)  (4ULL * (src))
#define PLIC_ENABLE(ctx)    (0x2000ULL + 0x80ULL * (ctx))
#define PLIC_THRESHOLD(ctx) (0x200000ULL + 0x1000ULL * (ctx))
#define PLIC_CLAIM(ctx)     (0x200004ULL + 0x1000ULL * (ctx))

#define CTX_M 0
#define CTX_S 1
#define NSOURCES 32 /* one enable word: sources 1..31 (orbit-rv has 31) */

static uint64_t plic;

void irqchip_init(void)
{
    unsigned src;
    plic = plat.plic_base;
    for (src = 1; src < NSOURCES; src++)
        mmio_write32(plic + PLIC_PRIORITY(src), 0);
    mmio_write32(plic + PLIC_ENABLE(CTX_M), 0);
    mmio_write32(plic + PLIC_ENABLE(CTX_S), 0);
    mmio_write32(plic + PLIC_THRESHOLD(CTX_M), 7); /* M-mode takes no external IRQs */
    mmio_write32(plic + PLIC_THRESHOLD(CTX_S), 0);
    csr_set(sie, SIE_SEIE);
    kprintf("[plic] PLIC at %p, context %d (hart 0 S-mode), sources 1-%d\n", (void *)plic,
            CTX_S, NSOURCES - 1);
}

void irqchip_enable(int irq)
{
    if (irq <= 0 || irq >= NSOURCES)
        return;
    mmio_write32(plic + PLIC_PRIORITY(irq), 1);
    mmio_write32(plic + PLIC_ENABLE(CTX_S),
                 mmio_read32(plic + PLIC_ENABLE(CTX_S)) | (1u << irq));
}

uint32_t plic_num_sources(void)
{
    return NSOURCES - 1;
}

void plic_irq(void)
{
    uint32_t src;
    while ((src = mmio_read32(plic + PLIC_CLAIM(CTX_S))) != 0) {
        if ((int)src == plat.uart_irq)
            uart_irq_handler();
        else
            kprintf("[irq] unexpected PLIC source %u\n", src);
        mmio_write32(plic + PLIC_CLAIM(CTX_S), src);
    }
}
