#include "buddy.h"

#define META_FREE  0x80 /* page heads a free block of order (meta & 0x3f) */
#define META_ALLOC 0x40 /* page heads an allocated block */
#define META_ORDER(m) ((m) & 0x3f)

static inline uintptr_t idx_addr(const struct buddy *b, size_t idx)
{
    return b->base + (idx << PAGE_SHIFT);
}

static inline struct list_node *idx_node(const struct buddy *b, size_t idx)
{
    return (struct list_node *)idx_addr(b, idx);
}

static int addr_idx(const struct buddy *b, const void *p, size_t *idx)
{
    uintptr_t a = (uintptr_t)p;
    if (a < b->base || (a & (PAGE_SIZE - 1)))
        return -1;
    *idx = (a - b->base) >> PAGE_SHIFT;
    return *idx < b->npages ? 0 : -1;
}

static void push_free(struct buddy *b, size_t idx, unsigned order)
{
    b->meta[idx] = (uint8_t)(META_FREE | order);
    list_push_back(&b->free[order], idx_node(b, idx));
    b->nfree[order]++;
    b->free_pages += (size_t)1 << order;
}

static void unlink_free(struct buddy *b, size_t idx, unsigned order)
{
    list_remove(idx_node(b, idx));
    b->meta[idx] = 0;
    b->nfree[order]--;
    b->free_pages -= (size_t)1 << order;
}

void buddy_init(struct buddy *b, uintptr_t base, size_t npages, uint8_t *meta)
{
    unsigned i;
    size_t p;
    b->base = base;
    b->npages = npages;
    b->meta = meta;
    b->free_pages = 0;
    b->managed_pages = 0;
    for (i = 0; i < BUDDY_ORDERS; i++) {
        list_init(&b->free[i]);
        b->nfree[i] = 0;
    }
    for (p = 0; p < npages; p++)
        meta[p] = 0; /* reserved: neither free nor a freeable allocation */
}

/* Free the block headed by idx with the given order, coalescing upward. */
static void free_block(struct buddy *b, size_t idx, unsigned order)
{
    while (order + 1 < BUDDY_ORDERS) {
        size_t bud = idx ^ ((size_t)1 << order);
        if (bud + ((size_t)1 << order) > b->npages)
            break;
        if (b->meta[bud] != (uint8_t)(META_FREE | order))
            break;
        unlink_free(b, bud, order);
        if (bud < idx)
            idx = bud;
        order++;
    }
    push_free(b, idx, order);
}

/* End (exclusive page index) of the free or allocated block that contains
 * page p, or 0 if p is not managed. Blocks are naturally aligned, so the
 * head of an order-o block containing p is p rounded down to 2^o; only
 * heads carry metadata. */
static size_t managed_block_end(const struct buddy *b, size_t p)
{
    unsigned o;
    for (o = 0; o < BUDDY_ORDERS; o++) {
        size_t h = p & ~(((size_t)1 << o) - 1);
        uint8_t m = b->meta[h];
        if ((m & (META_FREE | META_ALLOC)) && META_ORDER(m) == o)
            return h + ((size_t)1 << o);
    }
    return 0;
}

/* Does any managed block start inside [idx, idx + n)? */
static int has_head_in(const struct buddy *b, size_t idx, size_t n)
{
    size_t i;
    for (i = idx; i < idx + n; i++)
        if (b->meta[i] & (META_FREE | META_ALLOC))
            return 1;
    return 0;
}

void buddy_add_range(struct buddy *b, uintptr_t addr, size_t npages)
{
    size_t idx, end;
    uintptr_t a = (addr + PAGE_SIZE - 1) & ~(uintptr_t)(PAGE_SIZE - 1);
    if (a > addr)
        npages = npages ? npages - 1 : 0;
    if (a < b->base)
        return;
    idx = (a - b->base) >> PAGE_SHIFT;
    if (idx >= b->npages)
        return;
    end = idx + npages;
    if (end > b->npages)
        end = b->npages;
    /* Insert the largest naturally aligned blocks that fit; coalescing in
     * free_block() merges them with neighbours added earlier. Pages that are
     * already managed (free or allocated) are skipped, so overlapping adds
     * are harmless: a candidate block overlaps an existing block exactly
     * when it starts inside that block or that block starts inside it. */
    while (idx < end) {
        unsigned order = 0;
        size_t covered = managed_block_end(b, idx);
        if (covered) {
            idx = covered; /* skip the whole existing block */
            continue;
        }
        while (order + 1 < BUDDY_ORDERS &&
               (idx & (((size_t)2 << order) - 1)) == 0 &&
               idx + ((size_t)2 << order) <= end)
            order++;
        while (order > 0 && has_head_in(b, idx, (size_t)1 << order))
            order--;
        b->managed_pages += (size_t)1 << order;
        free_block(b, idx, order);
        idx += (size_t)1 << order;
    }
}

void *buddy_alloc(struct buddy *b, unsigned order)
{
    unsigned o;
    size_t idx;
    if (order >= BUDDY_ORDERS)
        return NULL;
    for (o = order; o < BUDDY_ORDERS; o++)
        if (!list_empty(&b->free[o]))
            break;
    if (o == BUDDY_ORDERS)
        return NULL;
    idx = ((uintptr_t)b->free[o].next - b->base) >> PAGE_SHIFT;
    unlink_free(b, idx, o);
    while (o > order) {
        o--;
        push_free(b, idx + ((size_t)1 << o), o); /* upper half stays free */
    }
    b->meta[idx] = (uint8_t)(META_ALLOC | order);
    return (void *)idx_addr(b, idx);
}

int buddy_free(struct buddy *b, void *ptr)
{
    size_t idx;
    uint8_t m;
    if (!ptr || addr_idx(b, ptr, &idx))
        return -1;
    m = b->meta[idx];
    if (!(m & META_ALLOC))
        return -1;
    b->meta[idx] = 0;
    free_block(b, idx, META_ORDER(m));
    return 0;
}

int buddy_block_order(const struct buddy *b, const void *ptr)
{
    size_t idx;
    if (addr_idx(b, ptr, &idx) || !(b->meta[idx] & META_ALLOC))
        return -1;
    return META_ORDER(b->meta[idx]);
}

size_t buddy_free_pages(const struct buddy *b)
{
    return b->free_pages;
}

unsigned buddy_order_for(size_t bytes)
{
    unsigned order = 0;
    size_t pages = (bytes + PAGE_SIZE - 1) >> PAGE_SHIFT;
    while (((size_t)1 << order) < pages)
        order++;
    return order;
}

int buddy_check(const struct buddy *b)
{
    unsigned o;
    size_t total = 0, heads = 0, i;
    for (o = 0; o < BUDDY_ORDERS; o++) {
        size_t n = 0;
        const struct list_node *it;
        for (it = b->free[o].next; it != &b->free[o]; it = it->next) {
            size_t idx = ((uintptr_t)it - b->base) >> PAGE_SHIFT;
            if (idx >= b->npages || b->meta[idx] != (uint8_t)(META_FREE | o))
                return -1;
            if (idx & (((size_t)1 << o) - 1))
                return -2; /* misaligned block */
            n++;
        }
        if (n != b->nfree[o])
            return -3;
        total += n << o;
    }
    for (i = 0; i < b->npages; i++)
        if (b->meta[i] & META_FREE)
            heads++;
    {
        size_t listed = 0;
        for (o = 0; o < BUDDY_ORDERS; o++)
            listed += b->nfree[o];
        if (listed != heads)
            return -4;
    }
    return total == b->free_pages ? 0 : -5;
}
