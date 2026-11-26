#include "a64pt.h"

static unsigned idx_at(uint64_t va, int level)
{
    return (unsigned)((va >> (39 - 9 * level)) & 511);
}

int a64pt_map(struct a64pt_space *s, uint64_t va, uint64_t pa, uint64_t size, uint64_t prot)
{
    uint64_t end;
    if (size == 0)
        return 0;
    /* Round the end up *before* rounding va down: an unaligned range must
     * still cover the last page it touches. (Computing the end as
     * ALIGN_DOWN(va) + ALIGN_UP(size) dropped that page.) */
    end = (va + size + A64_PAGE - 1) & ~(A64_PAGE - 1);
    va &= ~(A64_PAGE - 1);
    pa &= ~(A64_PAGE - 1);
    while (va < end) {
        uint64_t *t = s->l0;
        uint64_t step = A64_PAGE;
        int level;
        for (level = 0; level < 3; level++) {
            uint64_t *e = &t[idx_at(va, level)];
            if (level == 2) {
                if (*e == 0 && !(va & (A64_BLOCK - 1)) && !(pa & (A64_BLOCK - 1)) &&
                    end - va >= A64_BLOCK) {
                    *e = pa | prot | PTE_VALID; /* block descriptor */
                    step = A64_BLOCK;
                    goto advance;
                }
                if ((*e & PTE_VALID) && !(*e & PTE_TABLE)) {
                    /* already covered by a block: skip to its end */
                    step = A64_BLOCK - (va & (A64_BLOCK - 1));
                    goto advance;
                }
            }
            if (!(*e & PTE_VALID)) {
                uint64_t *n = s->alloc_table(s->ctx);
                if (!n)
                    return -1;
                *e = (uint64_t)(uintptr_t)n | PTE_VALID | PTE_TABLE;
            }
            t = (uint64_t *)(uintptr_t)(*e & PTE_ADDR_MASK);
        }
        {
            uint64_t *e = &t[idx_at(va, 3)];
            if (!(*e & PTE_VALID))
                *e = pa | prot | PTE_VALID | PTE_TABLE;
        }
    advance:
        va += step;
        pa += step;
    }
    return 0;
}

uint64_t a64pt_walk(const uint64_t *l0, uint64_t va, uint64_t *desc_out)
{
    const uint64_t *t = l0;
    int level;
    for (level = 0; level < 4; level++) {
        uint64_t d = t[idx_at(va, level)];
        if (!(d & PTE_VALID))
            return ~0ULL;
        if (level == 3) {
            if (!(d & PTE_TABLE))
                return ~0ULL; /* reserved encoding at L3 */
            if (desc_out)
                *desc_out = d;
            return (d & PTE_ADDR_MASK) | (va & (A64_PAGE - 1));
        }
        if (!(d & PTE_TABLE)) {
            if (level != 2)
                return ~0ULL; /* this builder never makes L0/L1 blocks */
            if (desc_out)
                *desc_out = d;
            return (d & PTE_ADDR_MASK & ~(A64_BLOCK - 1)) | (va & (A64_BLOCK - 1));
        }
        t = (const uint64_t *)(uintptr_t)(d & PTE_ADDR_MASK);
    }
    return ~0ULL;
}
