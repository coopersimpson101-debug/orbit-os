#include <stdint.h>
#include "test.h"
#include "buddy.h"

#define NPAGES 1024 /* 4 MiB: exactly one order-10 block */

static struct buddy b;
static uint8_t meta[NPAGES];
static void *raw;
static uintptr_t base;

static void setup(void)
{
    if (!raw)
        base = (uintptr_t)aligned_pages(NPAGES, &raw);
    buddy_init(&b, base, NPAGES, meta);
}

static void test_add_and_coalesce(void)
{
    setup();
    CHECK(buddy_free_pages(&b) == 0);
    /* Add in two halves; they must merge into one order-10 block. */
    buddy_add_range(&b, base, NPAGES / 2);
    buddy_add_range(&b, base + (NPAGES / 2) * PAGE_SIZE, NPAGES / 2);
    CHECK(buddy_free_pages(&b) == NPAGES);
    CHECK(b.nfree[10] == 1);
    CHECK(buddy_check(&b) == 0);
}

static void test_split_and_merge(void)
{
    void *p0, *p1, *p2;
    setup();
    buddy_add_range(&b, base, NPAGES);
    p0 = buddy_alloc(&b, 0);
    CHECK(p0 == (void *)base);
    CHECK(buddy_free_pages(&b) == NPAGES - 1);
    /* splitting order 10 down to 0 leaves one free block at orders 0..9 */
    for (unsigned o = 0; o < 10; o++)
        CHECK(b.nfree[o] == 1);
    CHECK(buddy_check(&b) == 0);
    p1 = buddy_alloc(&b, 0);
    CHECK(p1 == (void *)(base + PAGE_SIZE));
    p2 = buddy_alloc(&b, 3);
    CHECK(((uintptr_t)p2 - base) % (8 * PAGE_SIZE) == 0); /* naturally aligned */
    CHECK(buddy_block_order(&b, p2) == 3);
    CHECK(buddy_free(&b, p0) == 0);
    CHECK(buddy_free(&b, p1) == 0);
    CHECK(buddy_free(&b, p2) == 0);
    CHECK(buddy_free_pages(&b) == NPAGES);
    CHECK(b.nfree[10] == 1);
    CHECK(buddy_check(&b) == 0);
}

static void test_errors(void)
{
    void *p;
    setup();
    buddy_add_range(&b, base, NPAGES);
    p = buddy_alloc(&b, 2);
    CHECK(buddy_free(&b, p) == 0);
    CHECK(buddy_free(&b, p) == -1);                        /* double free */
    CHECK(buddy_free(&b, (char *)p + 1) == -1);            /* misaligned */
    CHECK(buddy_free(&b, (void *)(base - PAGE_SIZE)) == -1);/* out of range */
    CHECK(buddy_free(&b, NULL) == -1);
    CHECK(buddy_alloc(&b, BUDDY_ORDERS) == NULL);
    CHECK(buddy_check(&b) == 0);
}

static void test_exhaustion_and_holes(void)
{
    void *pages[NPAGES];
    int n = 0;
    setup();
    /* Leave a reserved hole (e.g. the DTB) of 3 pages at index 100. */
    buddy_add_range(&b, base, 100);
    buddy_add_range(&b, base + 103 * PAGE_SIZE, NPAGES - 103);
    CHECK(buddy_free_pages(&b) == NPAGES - 3);
    CHECK(buddy_check(&b) == 0);
    while ((pages[n] = buddy_alloc(&b, 0)) != NULL)
        n++;
    CHECK(n == NPAGES - 3);
    for (int i = 0; i < n; i++) {
        uintptr_t idx = ((uintptr_t)pages[i] - base) / PAGE_SIZE;
        CHECK(idx < 100 || idx >= 103); /* never hands out the hole */
    }
    for (int i = n - 1; i >= 0; i -= 2)
        CHECK(buddy_free(&b, pages[i]) == 0);
    for (int i = n - 2; i >= 0; i -= 2)
        CHECK(buddy_free(&b, pages[i]) == 0);
    CHECK(buddy_free_pages(&b) == NPAGES - 3);
    CHECK(buddy_check(&b) == 0);
    CHECK(b.nfree[10] == 0); /* the hole prevents a full order-10 merge */
}

static void test_random_stress(void)
{
    struct { void *p; unsigned order; } live[256];
    int nlive = 0;
    uint32_t seed = 12345;
    setup();
    buddy_add_range(&b, base, NPAGES);
    for (int it = 0; it < 20000; it++) {
        seed = seed * 1103515245u + 12345u;
        if ((seed >> 16) % 3 != 0 && nlive < 256) {
            unsigned order = (seed >> 8) % 5;
            void *p = buddy_alloc(&b, order);
            if (p) {
                /* no overlap with any live block */
                for (int j = 0; j < nlive; j++) {
                    uintptr_t a0 = (uintptr_t)p, a1 = a0 + (PAGE_SIZE << order);
                    uintptr_t b0 = (uintptr_t)live[j].p, b1 = b0 + (PAGE_SIZE << live[j].order);
                    CHECK(a1 <= b0 || b1 <= a0);
                }
                live[nlive].p = p;
                live[nlive++].order = order;
            }
        } else if (nlive) {
            int k = (int)((seed >> 4) % (uint32_t)nlive);
            CHECK(buddy_free(&b, live[k].p) == 0);
            live[k] = live[--nlive];
        }
    }
    while (nlive)
        CHECK(buddy_free(&b, live[--nlive].p) == 0);
    CHECK(buddy_free_pages(&b) == NPAGES);
    CHECK(b.nfree[10] == 1);
    CHECK(buddy_check(&b) == 0);
}

/* Regression: buddy_add_range() over pages that are already managed used to
 * insert them a second time when the range started inside an existing
 * block (found by proofstack's Lean model: Buddy.bug_double_allocation).
 * With a 4-page span, add(0,4) then add(1,1) gave 5 free pages, passed
 * buddy_check(), and alloc(0) / alloc(2) returned page 1 twice. */
static void test_overlapping_add_is_ignored(void)
{
    void *p0, *p2;
    setup();
    buddy_init(&b, base, 4, meta);
    buddy_add_range(&b, base, 4);
    buddy_add_range(&b, base + 1 * PAGE_SIZE, 1);
    CHECK(buddy_free_pages(&b) == 4);
    CHECK(b.nfree[0] == 0 && b.nfree[2] == 1);
    CHECK(buddy_check(&b) == 0);
    p0 = buddy_alloc(&b, 0);
    p2 = buddy_alloc(&b, 2);
    CHECK(p0 == (void *)base);
    CHECK(p2 == NULL); /* only 3 pages left: no second, overlapping block */
    /* adding over allocated pages is ignored too */
    buddy_add_range(&b, base, 4);
    CHECK(buddy_free_pages(&b) == 3);
    CHECK(buddy_check(&b) == 0);
    CHECK(buddy_free(&b, p0) == 0);
    CHECK(buddy_free_pages(&b) == 4 && b.nfree[2] == 1);
}

/* Random, overlapping adds: the managed set must be exactly the union of
 * the ranges, and allocating everything must hand out each page once. */
static void test_random_overlapping_adds(void)
{
    static uint8_t want[NPAGES], seen[NPAGES];
    uint32_t seed = 777;
    for (int round = 0; round < 200; round++) {
        size_t n = 0, got = 0;
        void *p;
        setup();
        memset(want, 0, sizeof want);
        memset(seen, 0, sizeof seen);
        for (int k = 0; k < 6; k++) {
            seed = seed * 1103515245u + 12345u;
            size_t a = (seed >> 8) % NPAGES;
            seed = seed * 1103515245u + 12345u;
            size_t len = (seed >> 8) % (NPAGES - a + 1);
            buddy_add_range(&b, base + a * PAGE_SIZE, len);
            for (size_t i = a; i < a + len; i++)
                want[i] = 1;
            CHECK(buddy_check(&b) == 0);
        }
        for (size_t i = 0; i < NPAGES; i++)
            n += want[i];
        CHECK(buddy_free_pages(&b) == n);
        while ((p = buddy_alloc(&b, 0)) != NULL) {
            size_t i = ((uintptr_t)p - base) / PAGE_SIZE;
            CHECK(want[i] && !seen[i]);
            seen[i] = 1;
            got++;
        }
        CHECK(got == n);
    }
}

static void test_order_for(void)
{
    CHECK(buddy_order_for(1) == 0);
    CHECK(buddy_order_for(4096) == 0);
    CHECK(buddy_order_for(4097) == 1);
    CHECK(buddy_order_for(16 * 1024) == 2);
    CHECK(buddy_order_for(16 * 1024 + 1) == 3);
}

int main(void)
{
    RUN(test_add_and_coalesce);
    RUN(test_split_and_merge);
    RUN(test_errors);
    RUN(test_exhaustion_and_holes);
    RUN(test_random_stress);
    RUN(test_overlapping_add_is_ignored);
    RUN(test_random_overlapping_adds);
    RUN(test_order_for);
    free(raw);
    TEST_REPORT("test_buddy");
}
