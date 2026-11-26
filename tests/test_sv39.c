#include <stdint.h>
#include "test.h"
#include "sv39.h"

#define NTAB 16

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

static void fresh(struct sv39_space *s, int max_tables)
{
    if (!pool)
        pool = aligned_pages(NTAB, &raw);
    used = 0;
    limit = max_tables;
    s->alloc_table = alloc_table;
    s->ctx = NULL;
    s->root = alloc_table(NULL);
}

static void test_pte_bits(void)
{
    uint64_t pte = sv39_leaf(0x80001000ULL, PTE_R | PTE_X);
    CHECK(pte == ((0x80001ULL << 10) | PTE_R | PTE_X | PTE_A | PTE_D | PTE_V));
    CHECK(sv39_is_leaf(pte));
    CHECK(sv39_pte_pa(pte) == 0x80001000ULL);
    /* A and D are always pre-set (Svade), V always set; stray bits dropped */
    pte = sv39_leaf(0x1000, PTE_R | PTE_W | PTE_U | (1ULL << 60));
    CHECK((pte & (PTE_A | PTE_D | PTE_V)) == (PTE_A | PTE_D | PTE_V));
    CHECK((pte >> 54) == 0);
    pte = sv39_table(0x80042000ULL);
    CHECK(pte == ((0x80042ULL << 10) | PTE_V));
    CHECK(!sv39_is_leaf(pte));
    CHECK(sv39_satp(0x80003000ULL, 0) == ((8ULL << 60) | 0x80003ULL));
    CHECK(sv39_satp(0x80003000ULL, 5) == ((8ULL << 60) | (5ULL << 44) | 0x80003ULL));
}

static void test_va(void)
{
    CHECK(sv39_vpn(0x80000000ULL, 2) == 2);
    CHECK(sv39_vpn(0x80201000ULL, 1) == 1);
    CHECK(sv39_vpn(0x80201000ULL, 0) == 1);
    CHECK(sv39_vpn(0xffffffc080000000ULL, 2) == 258);
    CHECK(sv39_va_canonical(0));
    CHECK(sv39_va_canonical(0x3fffffffffULL));
    CHECK(!sv39_va_canonical(0x4000000000ULL));
    CHECK(sv39_va_canonical(0xffffffc000000000ULL));
    CHECK(!sv39_va_canonical(0xdead00000000ULL));
    CHECK(sv39_level_size(0) == 4096 && sv39_level_size(1) == SV39_MEGA &&
          sv39_level_size(2) == SV39_GIGA);
}

static void test_map_and_walk(void)
{
    struct sv39_space s;
    uint64_t pte;
    int level;
    char perm[8];
    fresh(&s, NTAB);

    /* text RX then data RW at 4 KiB, like the kernel image */
    CHECK(sv39_map(&s, 0x80000000ULL, 0x80000000ULL, 0x2000, PTE_R | PTE_X, 0) == 0);
    CHECK(sv39_map(&s, 0x80002000ULL, 0x80002000ULL, 0x1001, PTE_R | PTE_W, 0) == 0);
    CHECK(used == 3); /* root + L1 + L0 */
    CHECK(sv39_walk(s.root, 0x80000ffcULL, &pte, &level) == 0x80000ffcULL);
    CHECK(level == 0);
    CHECK_STR(sv39_perm_str(pte, perm), "r-x--ad");
    CHECK(sv39_walk(s.root, 0x80003abcULL, &pte, &level) == 0x80003abcULL); /* rounded up */
    CHECK_STR(sv39_perm_str(pte, perm), "rw---ad");
    CHECK(sv39_walk(s.root, 0x80004000ULL, NULL, NULL) == ~0ULL);

    /* a covering RW map never overwrites the finer mappings */
    CHECK(sv39_map(&s, 0x80000000ULL, 0x80000000ULL, 0x10000, PTE_R | PTE_W, 0) == 0);
    sv39_walk(s.root, 0x80001000ULL, &pte, NULL);
    CHECK_STR(sv39_perm_str(pte, perm), "r-x--ad");
    sv39_walk(s.root, 0x8000f000ULL, &pte, NULL);
    CHECK_STR(sv39_perm_str(pte, perm), "rw---ad");

    /* megapages when aligned (devices) */
    CHECK(sv39_map(&s, 0x0c000000ULL, 0x0c000000ULL, 0x400000, PTE_R | PTE_W, 1) == 0);
    CHECK(used == 4); /* + L1 for the first GiB */
    CHECK(sv39_walk(s.root, 0x0c3ffffcULL, &pte, &level) == 0x0c3ffffcULL);
    CHECK(level == 1);
    /* max_level 0 forces 4 KiB pages even when aligned */
    CHECK(sv39_map(&s, 0x10000000ULL, 0x10000000ULL, SV39_MEGA, PTE_R | PTE_W, 0) == 0);
    CHECK(used == 5);
    sv39_walk(s.root, 0x10000000ULL, NULL, &level);
    CHECK(level == 0);

    /* higher-half alias: VA != PA, gigapage */
    CHECK(sv39_map(&s, 0xffffffc080000000ULL, 0x80000000ULL, SV39_GIGA, PTE_R | PTE_W, 2) == 0);
    CHECK(used == 5); /* gigapage sits in the root */
    CHECK(sv39_walk(s.root, 0xffffffc080123458ULL, &pte, &level) == 0x80123458ULL);
    CHECK(level == 2 && !(pte & PTE_X));

    /* user page */
    CHECK(sv39_map(&s, 0x80010000ULL, 0x80010000ULL, 0x1000, PTE_R | PTE_W | PTE_U, 0) == 0);
    sv39_walk(s.root, 0x80010000ULL, &pte, NULL);
    CHECK_STR(sv39_perm_str(pte, perm), "rw-u-ad");
}

static void test_errors(void)
{
    struct sv39_space s;
    fresh(&s, 2);
    /* non-canonical VA */
    CHECK(sv39_map(&s, 0xdead00000000ULL, 0, 0x1000, PTE_R, 0) == -1);
    /* table pool exhaustion: root + one L1 allowed, an L0 is needed */
    CHECK(sv39_map(&s, 0x80000000ULL, 0x80000000ULL, 0x1000, PTE_R, 0) == -1);
    /* nothing mapped -> walk fails; non-canonical walk fails */
    CHECK(sv39_walk(s.root, 0x80000000ULL, NULL, NULL) == ~0ULL);
    CHECK(sv39_walk(s.root, 0xdead00000000ULL, NULL, NULL) == ~0ULL);
    /* size 0 is a no-op */
    CHECK(sv39_map(&s, 0x80000000ULL, 0x80000000ULL, 0, PTE_R, 0) == 0);

    /* hand-built bad entries: misaligned megapage, W without R */
    fresh(&s, NTAB);
    {
        uint64_t *l1 = alloc_table(NULL);
        s.root[2] = sv39_table((uint64_t)(uintptr_t)l1);
        l1[0] = sv39_leaf(0x80001000ULL, PTE_R);      /* 2 MiB leaf, PA not aligned */
        l1[1] = ((0x80200ULL) << 10) | PTE_W | PTE_V; /* reserved encoding */
        CHECK(sv39_walk(s.root, 0x80000000ULL, NULL, NULL) == ~0ULL);
        CHECK(sv39_walk(s.root, 0x80200000ULL, NULL, NULL) == ~0ULL);
    }
}

int main(void)
{
    RUN(test_pte_bits);
    RUN(test_va);
    RUN(test_map_and_walk);
    RUN(test_errors);
    free(raw);
    TEST_REPORT("test_sv39");
}
