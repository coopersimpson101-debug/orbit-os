/* Stage-1 EL1 translation: 4 KiB granule, 48-bit VA, 4-level tables
 * (L0..L3). TTBR0 holds the identity map (kernel runs here), with the
 * kernel image mapped at page granularity so text/rodata/data/user
 * sections get distinct permissions. TTBR1 holds a higher-half linear
 * alias of RAM at KERNEL_VA_BASE (groundwork for moving the kernel up). */
#include "arch.h"
#include "kernel.h"
#include "mm.h"

/* MAIR attribute indices */
#define MT_DEVICE_nGnRnE 0
#define MT_DEVICE_nGnRE  1
#define MT_NORMAL        2
#define MT_NORMAL_NC     3
#define MAIR_VALUE ((0x00ULL << (8 * MT_DEVICE_nGnRnE)) | \
                    (0x04ULL << (8 * MT_DEVICE_nGnRE)) |  \
                    (0xffULL << (8 * MT_NORMAL)) |        \
                    (0x44ULL << (8 * MT_NORMAL_NC)))

#define PTE_VALID   (1ULL << 0)
#define PTE_TABLE   (1ULL << 1) /* table at L0-L2, page at L3 */
#define PTE_ATTR(i) ((uint64_t)(i) << 2)
#define PTE_AP_EL0  (1ULL << 6) /* EL0 accessible */
#define PTE_AP_RO   (1ULL << 7)
#define PTE_SH_IS   (3ULL << 8)
#define PTE_AF      (1ULL << 10)
#define PTE_PXN     (1ULL << 53)
#define PTE_UXN     (1ULL << 54)
#define PTE_ADDR_MASK 0x0000fffffffff000ULL

#define NORMAL      (PTE_ATTR(MT_NORMAL) | PTE_SH_IS | PTE_AF)
#define PROT_KDATA  (NORMAL | PTE_PXN | PTE_UXN)
#define PROT_KTEXT  (NORMAL | PTE_AP_RO | PTE_UXN)
#define PROT_KRO    (NORMAL | PTE_AP_RO | PTE_PXN | PTE_UXN)
#define PROT_UTEXT  (NORMAL | PTE_AP_RO | PTE_AP_EL0 | PTE_PXN)
#define PROT_UDATA  (NORMAL | PTE_AP_EL0 | PTE_PXN | PTE_UXN)
#define PROT_DEVICE (PTE_ATTR(MT_DEVICE_nGnRE) | PTE_AF | PTE_PXN | PTE_UXN)

#define SZ_2M 0x200000ULL
#define SZ_4K 0x1000ULL
#define MAX_RAM_MAP (4ULL << 30)

#define NTABLES 48
static uint64_t pt_pool[NTABLES][512] __attribute__((aligned(4096)));
static size_t pt_used;
static uint64_t *l0_lo, *l0_hi;
static int enabled;

static uint64_t *alloc_table(void)
{
    uint64_t *t;
    if (pt_used >= NTABLES)
        panic("mmu: page-table pool exhausted");
    t = pt_pool[pt_used++];
    k_memset(t, 0, 4096);
    return t;
}

/* Map [va, va+size) -> pa. Uses 2 MiB blocks where alignment allows and
 * the L2 slot is still empty; never overwrites an existing mapping, so
 * finer-grained regions mapped first keep their permissions. */
static void map_range(uint64_t *l0, uint64_t va, uint64_t pa, uint64_t size,
                      uint64_t prot)
{
    uint64_t end;
    va = ALIGN_DOWN(va, SZ_4K);
    pa = ALIGN_DOWN(pa, SZ_4K);
    end = va + ALIGN_UP(size, SZ_4K);
    while (va < end) {
        uint64_t *t = l0;
        int level;
        for (level = 0; level < 3; level++) {
            unsigned shift = 39 - 9 * level;
            unsigned idx = (va >> shift) & 511;
            if (level == 2) {
                if (t[idx] == 0 && !(va & (SZ_2M - 1)) && !(pa & (SZ_2M - 1)) &&
                    end - va >= SZ_2M) {
                    t[idx] = pa | prot | PTE_VALID; /* block descriptor */
                    goto advance_block;
                }
                if ((t[idx] & PTE_VALID) && !(t[idx] & PTE_TABLE))
                    goto advance_block; /* already covered by a block */
            }
            if (!(t[idx] & PTE_VALID))
                t[idx] = (uint64_t)(uintptr_t)alloc_table() | PTE_VALID | PTE_TABLE;
            t = (uint64_t *)(uintptr_t)(t[idx] & PTE_ADDR_MASK);
        }
        {
            unsigned idx = (va >> 12) & 511;
            if (!(t[idx] & PTE_VALID))
                t[idx] = pa | prot | PTE_VALID | PTE_TABLE;
        }
        va += SZ_4K;
        pa += SZ_4K;
        continue;
    advance_block: {
            uint64_t step = SZ_2M - (va & (SZ_2M - 1));
            va += step;
            pa += step;
        }
    }
}

static void map_ident(uint64_t start, uint64_t end, uint64_t prot)
{
    if (end > start)
        map_range(l0_lo, start, start, end - start, prot);
}

void mmu_init(void)
{
    uint64_t ram_map = plat.ram_size < MAX_RAM_MAP ? plat.ram_size : MAX_RAM_MAP;
    uint64_t parange, tcr, sctlr;

    l0_lo = alloc_table();
    l0_hi = alloc_table();

    /* 1. Kernel image, page granular, distinct permissions per section. */
    map_ident((uint64_t)__kernel_start, (uint64_t)__user_text_start, PROT_KTEXT);
    map_ident((uint64_t)__user_text_start, (uint64_t)__user_text_end, PROT_UTEXT);
    map_ident((uint64_t)__user_text_end, (uint64_t)__text_end, PROT_KTEXT);
    map_ident((uint64_t)__text_end, (uint64_t)__rodata_end, PROT_KRO);
    map_ident((uint64_t)__rodata_end, (uint64_t)__user_stack_start, PROT_KDATA);
    map_ident((uint64_t)__user_stack_start, (uint64_t)__user_stack_end, PROT_UDATA);
    map_ident((uint64_t)__user_stack_end, ALIGN_UP((uint64_t)_end, SZ_4K), PROT_KDATA);

    /* 2. The rest of RAM: normal write-back memory, never executable. */
    map_range(l0_lo, plat.ram_base, plat.ram_base, ram_map, PROT_KDATA);

    /* 3. DTB, in case the firmware put it outside the RAM range we map. */
    if (plat.dtb)
        map_range(l0_lo, (uint64_t)(uintptr_t)plat.dtb, (uint64_t)(uintptr_t)plat.dtb,
                  plat.dtb_size, PROT_KRO);

    /* 4. MMIO window (UART, GIC, ...) as Device-nGnRE. */
    map_range(l0_lo, plat.dev_base, plat.dev_base, plat.dev_size, PROT_DEVICE);

    /* 5. Higher-half linear alias of RAM in TTBR1. */
    map_range(l0_hi, KERNEL_VA_BASE + plat.ram_base, plat.ram_base, ram_map,
              PROT_KDATA);
    map_range(l0_hi, KERNEL_VA_BASE + plat.dev_base, plat.dev_base, plat.dev_size,
              PROT_DEVICE);

    parange = read_sysreg(id_aa64mmfr0_el1) & 0xf;
    if (parange > 5)
        parange = 5; /* 48-bit is the most a 4K/4-level config can use */
    tcr = (16ULL << 0) |        /* T0SZ: 48-bit user/identity space */
          (1ULL << 8) |         /* IRGN0 WBWA */
          (1ULL << 10) |        /* ORGN0 WBWA */
          (3ULL << 12) |        /* SH0 inner shareable */
          (0ULL << 14) |        /* TG0 4 KiB */
          (16ULL << 16) |       /* T1SZ */
          (1ULL << 24) | (1ULL << 26) | (3ULL << 28) |
          (2ULL << 30) |        /* TG1 4 KiB */
          (parange << 32);      /* IPS */

    dsb_ish();
    write_sysreg(mair_el1, MAIR_VALUE);
    write_sysreg(tcr_el1, tcr);
    write_sysreg(ttbr0_el1, (uint64_t)(uintptr_t)l0_lo);
    write_sysreg(ttbr1_el1, (uint64_t)(uintptr_t)l0_hi);
    isb();
    __asm__ volatile("tlbi vmalle1\n dsb ish\n isb" ::: "memory");
    __asm__ volatile("ic iallu\n dsb ish\n isb" ::: "memory");

    sctlr = read_sysreg(sctlr_el1);
    sctlr |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12); /* M, C, I */
    sctlr &= ~(1ULL << 1);                             /* no alignment checks */
    sctlr &= ~(1ULL << 19);                            /* WXN off (we set XN explicitly) */
    write_sysreg(sctlr_el1, sctlr);
    isb();
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

uint64_t mmu_translate(uint64_t va)
{
    uint64_t par;
    __asm__ volatile("at s1e1r, %0" : : "r"(va));
    isb();
    par = read_sysreg(par_el1);
    if (par & 1)
        return ~0ULL;
    return (par & PTE_ADDR_MASK) | (va & 0xfff);
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
