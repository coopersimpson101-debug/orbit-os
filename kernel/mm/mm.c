/* Glue: hands usable RAM to the buddy allocator and wires the heap to it. */
#include "arch.h"
#include "kernel.h"
#include "fdt.h"
#include "mm.h"

static struct buddy g_buddy;
static struct kheap g_heap;

struct range {
    uint64_t start, end;
};

static void *heap_alloc_pages(void *ctx, unsigned order)
{
    (void)ctx;
    return buddy_alloc(&g_buddy, order);
}

static void heap_free_pages(void *ctx, void *p)
{
    (void)ctx;
    buddy_free(&g_buddy, p);
}

static int add_reserved(struct range *r, int n, int max, uint64_t a, uint64_t size)
{
    if (n >= max || size == 0)
        return n;
    r[n].start = ALIGN_DOWN(a, PAGE_SIZE);
    r[n].end = ALIGN_UP(a + size, PAGE_SIZE);
    return n + 1;
}

void mm_init(void)
{
    uint64_t ram_end = plat.ram_base +
                       (plat.ram_size < (4ULL << 30) ? plat.ram_size : (4ULL << 30));
    uint64_t start = ALIGN_UP((uint64_t)(uintptr_t)_end, PAGE_SIZE);
    uint64_t npages, meta_bytes, base, cursor;
    struct range rsv[16];
    int nrsv = 0, i, j;
    struct kheap_page_source src = { heap_alloc_pages, heap_free_pages, 0 };

    if (start < plat.ram_base || start >= ram_end)
        panic("mm: kernel end %p outside RAM [%p, %p)", (void *)start,
              (void *)plat.ram_base, (void *)ram_end);

    /* Metadata byte array right after the kernel image. */
    npages = (ram_end - start) / PAGE_SIZE;
    meta_bytes = ALIGN_UP(npages, PAGE_SIZE);
    base = start + meta_bytes;
    npages = (ram_end - base) / PAGE_SIZE;
    buddy_init(&g_buddy, base, npages, (uint8_t *)(uintptr_t)start);

    if (plat.dtb)
        nrsv = add_reserved(rsv, nrsv, 16, (uint64_t)(uintptr_t)plat.dtb, plat.dtb_size);
    if (plat.dtb) {
        uint64_t a, s;
        for (i = 0; fdt_memreserve(plat.dtb, i, &a, &s) == 0; i++)
            nrsv = add_reserved(rsv, nrsv, 16, a, s);
    }
    /* insertion sort by start */
    for (i = 1; i < nrsv; i++)
        for (j = i; j > 0 && rsv[j].start < rsv[j - 1].start; j--) {
            struct range t = rsv[j];
            rsv[j] = rsv[j - 1];
            rsv[j - 1] = t;
        }

    cursor = base;
    for (i = 0; i <= nrsv; i++) {
        uint64_t hole_end = i < nrsv ? rsv[i].start : ram_end;
        if (hole_end > ram_end)
            hole_end = ram_end;
        if (hole_end > cursor)
            buddy_add_range(&g_buddy, cursor, (hole_end - cursor) / PAGE_SIZE);
        if (i < nrsv && rsv[i].end > cursor)
            cursor = rsv[i].end;
    }

    kheap_init(&g_heap, &src);
    kprintf("[mm] buddy: %lu pages free (%lu KiB) at [%p, %p), %d reserved range(s)\n",
            (uint64_t)buddy_free_pages(&g_buddy),
            (uint64_t)(buddy_free_pages(&g_buddy) * PAGE_SIZE >> 10), (void *)base,
            (void *)ram_end, nrsv);
}

void *page_alloc(unsigned order)
{
    uint64_t f = irq_save();
    void *p = buddy_alloc(&g_buddy, order);
    irq_restore(f);
    return p;
}

void page_free(void *p)
{
    uint64_t f = irq_save();
    if (buddy_free(&g_buddy, p))
        kprintf("[mm] bad page_free(%p)\n", p);
    irq_restore(f);
}

void *kmalloc(size_t n)
{
    uint64_t f = irq_save();
    void *p = kheap_alloc(&g_heap, n);
    irq_restore(f);
    return p;
}

void *kzalloc(size_t n)
{
    void *p = kmalloc(n);
    if (p)
        k_memset(p, 0, n);
    return p;
}

void kfree(void *p)
{
    uint64_t f = irq_save();
    if (kheap_free(&g_heap, p))
        kprintf("[mm] bad kfree(%p)\n", p);
    irq_restore(f);
}

const struct buddy *mm_buddy(void)
{
    return &g_buddy;
}

const struct kheap_stats *mm_heap_stats(void)
{
    return &g_heap.st;
}
