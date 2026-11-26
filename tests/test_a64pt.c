#include <stdint.h>
#include "test.h"
#include "a64pt.h"

#define NTAB 64
#define PROT_A (PTE_ATTR(2) | PTE_AF | PTE_PXN | PTE_UXN)
#define PROT_B (PTE_ATTR(2) | PTE_AF | PTE_AP_RO | PTE_UXN)

static uint64_t *pool;
static void *raw;
static int used, limit;

static uint64_t *alloc_table(void *ctx)
{
    uint64_t *t;
    (void)ctx;
    if (used >= limit)
        return NULL;
    t = pool + 512 * used++;
    memset(t, 0, 4096);
    return t;
}

static void fresh(struct a64pt_space *s, int max_tables)
{
    if (!pool)
        pool = aligned_pages(NTAB, &raw);
    used = 0;
    limit = max_tables;
    s->alloc_table = alloc_table;
    s->ctx = NULL;
    s->l0 = alloc_table(NULL);
}

static uint64_t attrs(uint64_t d)
{
    return d & ~PTE_ADDR_MASK & ~3ULL;
}

/* Regression: an unaligned range must cover the last page it touches.
 * map(0x1800, size 0x1000) used to map only the page at 0x1000 because the
 * end was computed as ALIGN_DOWN(va) + ALIGN_UP(size) (found by proofstack's
 * Lean model, Mmu.unaligned_bug; mmu_init maps the DTB this way). */
static void test_unaligned_range_covers_last_page(void)
{
    struct a64pt_space s;
    uint64_t d;
    fresh(&s, NTAB);
    CHECK(a64pt_map(&s, 0x1800, 0x1800, 0x1000, PROT_A) == 0);
    CHECK(a64pt_walk(s.l0, 0x1800, &d) == 0x1800 && attrs(d) == PROT_A);
    CHECK(a64pt_walk(s.l0, 0x27ff, NULL) == 0x27ff);
    CHECK(a64pt_walk(s.l0, 0x0fff, NULL) == ~0ULL);
    CHECK(a64pt_walk(s.l0, 0x2800, NULL) == 0x2800); /* same page: page granularity */
    CHECK(a64pt_walk(s.l0, 0x3000, NULL) == ~0ULL);
    /* a DTB-like blob: 0x10 bytes past a page boundary, 2 pages + 16 bytes */
    fresh(&s, NTAB);
    CHECK(a64pt_map(&s, 0x4f000ff0, 0x4f000ff0, 0x2020, PROT_B) == 0);
    for (uint64_t va = 0x4f000000; va < 0x4f004000; va += 0x1000)
        CHECK(a64pt_walk(s.l0, va, NULL) == va);
    CHECK(a64pt_walk(s.l0, 0x4f004000, NULL) == ~0ULL);
}

static void test_pages_and_blocks(void)
{
    struct a64pt_space s;
    uint64_t d;
    fresh(&s, NTAB);
    /* 2 MiB-aligned va and pa, 4 MiB + 3 pages: two blocks then pages */
    CHECK(a64pt_map(&s, 0x40000000, 0x80000000ULL, 0x403000, PROT_A) == 0);
    for (uint64_t off = 0; off < 0x403000; off += 0x1000)
        CHECK(a64pt_walk(s.l0, 0x40000000 + off, &d) == 0x80000000ULL + off && attrs(d) == PROT_A);
    CHECK(a64pt_walk(s.l0, 0x40403000, NULL) == ~0ULL);
    a64pt_walk(s.l0, 0x40000000, &d);
    CHECK(!(d & PTE_TABLE));                 /* block descriptor */
    a64pt_walk(s.l0, 0x40400000, &d);
    CHECK((d & PTE_TABLE) != 0);             /* page descriptor (L3) */
    /* misaligned pa forces pages everywhere */
    fresh(&s, NTAB);
    CHECK(a64pt_map(&s, 0x200000, 0x201000, 0x200000, PROT_A) == 0);
    a64pt_walk(s.l0, 0x200000, &d);
    CHECK((d & PTE_TABLE) != 0);
    CHECK(a64pt_walk(s.l0, 0x3ff123, NULL) == 0x400123);
}

static void test_first_mapping_wins(void)
{
    struct a64pt_space s;
    uint64_t d;
    fresh(&s, NTAB);
    CHECK(a64pt_map(&s, 0x40080000, 0x40080000, 0x2000, PROT_B) == 0);  /* fine region */
    CHECK(a64pt_map(&s, 0x40000000, 0x40000000, 0x400000, PROT_A) == 0); /* RAM over it */
    CHECK(a64pt_walk(s.l0, 0x40080000, &d) == 0x40080000 && attrs(d) == PROT_B);
    CHECK(a64pt_walk(s.l0, 0x40081fff, &d) == 0x40081fff && attrs(d) == PROT_B);
    CHECK(a64pt_walk(s.l0, 0x40082000, &d) == 0x40082000 && attrs(d) == PROT_A);
    CHECK(a64pt_walk(s.l0, 0x40200000, &d) == 0x40200000 && attrs(d) == PROT_A);
    /* a later range under an existing block keeps the block */
    CHECK(a64pt_map(&s, 0x40201000, 0x9000000, 0x1000, PROT_B) == 0);
    CHECK(a64pt_walk(s.l0, 0x40201000, &d) == 0x40201000 && attrs(d) == PROT_A);
}

static void test_higher_half_and_exhaustion(void)
{
    struct a64pt_space s;
    fresh(&s, NTAB);
    CHECK(a64pt_map(&s, 0xffff000040000000ULL, 0x40000000, 0x200000, PROT_A) == 0);
    CHECK(a64pt_walk(s.l0, 0xffff000040012345ULL, NULL) == 0x40012345);
    fresh(&s, 2); /* L0 + one more table: cannot reach L3 */
    CHECK(a64pt_map(&s, 0x1000, 0x1000, 0x1000, PROT_A) == -1);
    CHECK(a64pt_map(&s, 0x1000, 0x1000, 0, PROT_A) == 0);
}

int main(void)
{
    RUN(test_unaligned_range_covers_last_page);
    RUN(test_pages_and_blocks);
    RUN(test_first_mapping_wins);
    RUN(test_higher_half_and_exhaustion);
    free(raw);
    TEST_REPORT("test_a64pt");
}
