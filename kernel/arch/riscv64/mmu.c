/* Sv39 translation for S-mode, built with the portable helpers in
 * lib/sv39.c. The kernel keeps running from the identity map; the image is
 * mapped at 4 KiB granularity so every section gets its own permissions
 * (W^X), the devices and the DTB use 2 MiB megapages, and a higher-half
 * alias of RAM sits at KERNEL_VA_BASE (Sv39 upper half). Every leaf has
 * A and D pre-set because orbit-rv does not update them in hardware. */
#include "arch.h"
#include "kernel.h"
#include "mm.h"
#include "rv64_map.h"
#include "sv39.h"

/* Kernel identity: PROT_*; U = user accessible (S needs SUM to touch it). */
#define PROT_KTEXT  (PTE_R | PTE_X | PTE_G)
#define PROT_KRO    (PTE_R | PTE_G)
#define PROT_KDATA  (PTE_R | PTE_W | PTE_G)
#define PROT_UTEXT  (PTE_R | PTE_X | PTE_U)
#define PROT_UDATA  (PTE_R | PTE_W | PTE_U)
#define PROT_DEVICE (PTE_R | PTE_W | PTE_G)

/* root, L1 for the device GiB, L1 + L0 for RAM, L1 for the alias, + 1 spare */
#define NTABLES 6
static uint64_t pt_pool[NTABLES][SV39_ENTRIES] __attribute__((aligned(4096)));
static size_t pt_used;
static struct sv39_space kspace;
static int enabled;

static uint64_t *alloc_table(void *ctx)
{
    uint64_t *t;
    (void)ctx;
    if (pt_used >= NTABLES)
        return 0;
    t = pt_pool[pt_used++];
    k_memset(t, 0, sizeof(pt_pool[0]));
    return t;
}

static void map(uint64_t va, uint64_t pa, uint64_t size, uint64_t prot, int max_level)
{
    if (sv39_map(&kspace, va, pa, size, prot, max_level))
        panic("mmu: cannot map %p+%p (page-table pool exhausted?)", (void *)va,
              (void *)size);
}

static void map_ident(uint64_t start, uint64_t end, uint64_t prot)
{
    if (end > start)
        map(start, start, end - start, prot, 0);
}

/* Round out to whole megapages: cheap (no L0 tables) for MMIO and the DTB. */
static void map_mega(uint64_t va, uint64_t pa, uint64_t size, uint64_t prot)
{
    uint64_t start = ALIGN_DOWN(pa, SV39_MEGA);
    uint64_t end = ALIGN_UP(pa + size, SV39_MEGA);
    map(va - (pa - start), start, end - start, prot, 1);
}

void mmu_init(void)
{
    kspace.root = 0;
    kspace.alloc_table = alloc_table;
    kspace.ctx = 0;
    kspace.root = alloc_table(0);

    /* 1. Kernel image, page granular, distinct permissions per section. */
    map_ident((uint64_t)__kernel_start, (uint64_t)__user_text_start, PROT_KTEXT);
    map_ident((uint64_t)__user_text_start, (uint64_t)__user_text_end, PROT_UTEXT);
    map_ident((uint64_t)__user_text_end, (uint64_t)__text_end, PROT_KTEXT);
    map_ident((uint64_t)__text_end, (uint64_t)__rodata_end, PROT_KRO);
    map_ident((uint64_t)__rodata_end, (uint64_t)__user_stack_start, PROT_KDATA);
    map_ident((uint64_t)__user_stack_start, (uint64_t)__user_stack_end, PROT_UDATA);
    map_ident((uint64_t)__user_stack_end, ALIGN_UP((uint64_t)_end, SV39_PAGE), PROT_KDATA);

    /* 2. The rest of RAM: read/write, never executable. */
    map_ident(ALIGN_UP((uint64_t)_end, SV39_PAGE), plat.ram_base + plat.ram_size, PROT_KDATA);

    /* 3. DTB (QEMU puts it near the top of its RAM, outside our 256 KiB). */
    if (plat.dtb)
        map_mega((uint64_t)(uintptr_t)plat.dtb, (uint64_t)(uintptr_t)plat.dtb, plat.dtb_size,
                 PROT_KRO);

    /* 4. Devices: finisher (M-mode only, not mapped), PLIC, UART. */
    map_mega(plat.plic_base, plat.plic_base, RV64_PLIC_SIZE, PROT_DEVICE);
    map_mega(plat.uart_base, plat.uart_base, SV39_PAGE, PROT_DEVICE);

    /* 5. Higher-half alias of RAM, data only. */
    map_mega(KERNEL_VA_BASE + plat.ram_base, plat.ram_base, plat.ram_size, PROT_KDATA);

    csr_write(satp, sv39_satp((uint64_t)(uintptr_t)kspace.root, 0));
    sfence_vma();
    enabled = 1;
}

int mmu_enabled(void)
{
    return enabled;
}

size_t mmu_tables_used(void)
{
    return pt_used;
}

/* Like AT S1E1R on AArch64: the PA a kernel read of `va` would reach, or
 * ~0 if it would fault (unmapped, not readable, or a U page while SUM=0). */
uint64_t mmu_translate(uint64_t va)
{
    uint64_t pte, pa;
    if (!enabled)
        return va;
    pa = sv39_walk(kspace.root, va, &pte, 0);
    if (pa == ~0ULL || !(pte & PTE_R) || (pte & PTE_U))
        return ~0ULL;
    return pa;
}

int mmu_selftest_higher_half(void)
{
    volatile uint64_t *lo = kmalloc(sizeof(uint64_t));
    volatile uint64_t *hi;
    int ok;
    if (!lo)
        return 0;
    hi = phys_to_virt((uint64_t)(uintptr_t)lo);
    *lo = 0x0123456789abcdefULL;
    ok = (*hi == 0x0123456789abcdefULL);
    *hi = 0xfeedfacecafebeefULL;
    ok = ok && (*lo == 0xfeedfacecafebeefULL);
    ok = ok && (mmu_translate((uint64_t)(uintptr_t)hi) == (uint64_t)(uintptr_t)lo);
    kfree((void *)lo);
    return ok;
}
