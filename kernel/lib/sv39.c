#include "sv39.h"

#define PPN_MASK ((1ULL << 44) - 1)

uint64_t sv39_leaf(uint64_t pa, uint64_t perm)
{
    /* Svade: pre-set A and D so no access ever faults for bookkeeping. */
    return ((pa >> 12) << 10) | (perm & PTE_PERM_MASK) | PTE_A | PTE_D | PTE_V;
}

uint64_t sv39_table(uint64_t table_pa)
{
    return ((table_pa >> 12) << 10) | PTE_V; /* R=W=X=0: pointer to next level */
}

uint64_t sv39_pte_pa(uint64_t pte)
{
    return ((pte >> 10) & PPN_MASK) << 12;
}

int sv39_is_leaf(uint64_t pte)
{
    return (pte & PTE_V) && (pte & (PTE_R | PTE_X));
}

unsigned sv39_vpn(uint64_t va, int level)
{
    return (unsigned)(va >> (12 + 9 * level)) & (SV39_ENTRIES - 1);
}

int sv39_va_canonical(uint64_t va)
{
    uint64_t top = va >> 38; /* bits 63..38: all zeros or all ones */
    return top == 0 || top == (1ULL << 26) - 1;
}

uint64_t sv39_satp(uint64_t root_pa, unsigned asid)
{
    return SATP_MODE_SV39 | ((uint64_t)(asid & 0xffff) << 44) | (root_pa >> 12);
}

uint64_t sv39_level_size(int level)
{
    return SV39_PAGE << (9 * level);
}

static uint64_t *pte_table(uint64_t pte)
{
    return (uint64_t *)(uintptr_t)sv39_pte_pa(pte);
}

int sv39_map(struct sv39_space *s, uint64_t va, uint64_t pa, uint64_t size,
             uint64_t perm, int max_level)
{
    uint64_t end;
    if (size == 0)
        return 0;
    end = (va + size + SV39_PAGE - 1) & ~(SV39_PAGE - 1);
    va &= ~(SV39_PAGE - 1);
    pa &= ~(SV39_PAGE - 1);
    if (!sv39_va_canonical(va) || !sv39_va_canonical(end - 1))
        return -1;
    while (va < end) {
        uint64_t *t = s->root;
        uint64_t step = SV39_PAGE;
        int level;
        for (level = SV39_LEVELS - 1; level >= 0; level--) {
            uint64_t *e = &t[sv39_vpn(va, level)];
            uint64_t lsz = sv39_level_size(level);
            if (sv39_is_leaf(*e)) {
                /* Already mapped (by a block or page): skip to its end. */
                step = lsz - (va & (lsz - 1));
                break;
            }
            if (!(*e & PTE_V) && level <= max_level && !(va & (lsz - 1)) &&
                !(pa & (lsz - 1)) && end - va >= lsz) {
                *e = sv39_leaf(pa, perm);
                step = lsz;
                break;
            }
            if (level == 0)
                break; /* unreachable: an empty L0 slot is always mappable */
            if (!(*e & PTE_V)) {
                uint64_t *n = s->alloc_table(s->ctx);
                if (!n)
                    return -1;
                *e = sv39_table((uint64_t)(uintptr_t)n);
            }
            t = pte_table(*e);
        }
        va += step;
        pa += step;
    }
    return 0;
}

uint64_t sv39_walk(const uint64_t *root, uint64_t va, uint64_t *pte_out, int *level_out)
{
    const uint64_t *t = root;
    int level;
    if (!sv39_va_canonical(va))
        return ~0ULL;
    for (level = SV39_LEVELS - 1; level >= 0; level--) {
        uint64_t pte = t[sv39_vpn(va, level)];
        if (!(pte & PTE_V) || ((pte & PTE_W) && !(pte & PTE_R)))
            return ~0ULL;
        if (sv39_is_leaf(pte)) {
            uint64_t mask = sv39_level_size(level) - 1;
            if (sv39_pte_pa(pte) & mask)
                return ~0ULL; /* misaligned superpage: the hardware faults */
            if (pte_out)
                *pte_out = pte;
            if (level_out)
                *level_out = level;
            return sv39_pte_pa(pte) | (va & mask);
        }
        t = pte_table(pte);
    }
    return ~0ULL;
}

const char *sv39_perm_str(uint64_t pte, char buf[8])
{
    static const char flags[] = "rwxugad";
    int i;
    for (i = 0; i < 7; i++)
        buf[i] = (pte >> (i + 1)) & 1 ? flags[i] : '-';
    buf[7] = 0;
    return buf;
}
