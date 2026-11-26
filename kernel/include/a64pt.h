/* AArch64 stage-1 page-table builder (portable, host-tested).
 *
 * 4 KiB granule, 48-bit VA, four levels L0..L3 of 512 eight-byte
 * descriptors. 2 MiB block descriptors are used at L2 where alignment and
 * size allow. Tables are referenced by pointer and their address is used as
 * the output address in table descriptors, which holds both in the kernel
 * (tables are identity mapped) and in host unit tests (host buffers). */
#ifndef ORBIT_A64PT_H
#define ORBIT_A64PT_H

#include <stddef.h>
#include <stdint.h>

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

#define A64_PAGE  0x1000ULL
#define A64_BLOCK 0x200000ULL

struct a64pt_space {
    uint64_t *l0;
    /* Returns a zeroed, 4 KiB aligned table, or NULL when out of tables. */
    uint64_t *(*alloc_table)(void *ctx);
    void *ctx;
};

/* Map every 4 KiB page that [va, va + size) touches to the corresponding
 * page of pa (va and pa are rounded down to a page, the end is rounded up).
 * Uses 2 MiB blocks where va, pa and the remaining size allow and the L2
 * slot is still empty; never overwrites a valid descriptor, so regions
 * mapped first with finer permissions keep them. Returns 0, or -1 when
 * alloc_table fails. */
int a64pt_map(struct a64pt_space *s, uint64_t va, uint64_t pa, uint64_t size, uint64_t prot);

/* Software stage-1 walk: output address of `va`, or ~0 if it would fault.
 * If `desc_out` is non-NULL it receives the leaf descriptor. */
uint64_t a64pt_walk(const uint64_t *l0, uint64_t va, uint64_t *desc_out);

#endif
