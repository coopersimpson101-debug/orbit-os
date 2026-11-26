/* RISC-V Sv39 page-table helpers (portable, host-tested).
 *
 * Three levels of 512 eight-byte PTEs; level 2 is the root. A leaf can sit
 * at any level: 4 KiB page (level 0), 2 MiB megapage (1), 1 GiB gigapage (2).
 * Tables are referenced by pointer and their address is used as the PA in
 * the PTE, which holds both in the kernel (tables are identity mapped) and
 * in host unit tests (tables are host buffers).
 *
 * The orbit-rv CPU does not update PTE.A/D in hardware (Svade), so every
 * leaf built here has A and D pre-set. */
#ifndef ORBIT_SV39_H
#define ORBIT_SV39_H

#include <stddef.h>
#include <stdint.h>

#define PTE_V (1ULL << 0)
#define PTE_R (1ULL << 1)
#define PTE_W (1ULL << 2)
#define PTE_X (1ULL << 3)
#define PTE_U (1ULL << 4)
#define PTE_G (1ULL << 5)
#define PTE_A (1ULL << 6)
#define PTE_D (1ULL << 7)
#define PTE_PERM_MASK (PTE_R | PTE_W | PTE_X | PTE_U | PTE_G)

#define SV39_LEVELS  3
#define SV39_ENTRIES 512
#define SV39_PAGE    0x1000ULL
#define SV39_MEGA    0x200000ULL
#define SV39_GIGA    0x40000000ULL
#define SATP_MODE_SV39 (8ULL << 60)

/* PTE value of a leaf mapping `pa` with permissions `perm` (R/W/X/U/G). */
uint64_t sv39_leaf(uint64_t pa, uint64_t perm);
/* PTE value pointing at the next-level table at `table_pa`. */
uint64_t sv39_table(uint64_t table_pa);
uint64_t sv39_pte_pa(uint64_t pte);
int      sv39_is_leaf(uint64_t pte);
/* VPN[level] of a virtual address. */
unsigned sv39_vpn(uint64_t va, int level);
/* Bits 63..39 must all equal bit 38. */
int      sv39_va_canonical(uint64_t va);
uint64_t sv39_satp(uint64_t root_pa, unsigned asid);
/* Size of a leaf at `level` (4 KiB, 2 MiB, 1 GiB). */
uint64_t sv39_level_size(int level);

struct sv39_space {
    uint64_t *root;
    /* Returns a zeroed, 4 KiB aligned table, or NULL when out of tables. */
    uint64_t *(*alloc_table)(void *ctx);
    void *ctx;
};

/* Map [va, va + size) -> pa (page-aligned outwards). Megapages and
 * gigapages are used where alignment and size allow (`max_level` caps the
 * leaf size: 0 = 4 KiB pages only). Existing leaves are never overwritten,
 * so regions mapped first with finer permissions keep them. Returns 0, or
 * -1 for a non-canonical VA or when alloc_table fails. */
int sv39_map(struct sv39_space *s, uint64_t va, uint64_t pa, uint64_t size,
             uint64_t perm, int max_level);

/* Software walk. Returns the PA for `va`, or ~0 if no valid leaf maps it.
 * Optionally reports the leaf PTE and its level. */
uint64_t sv39_walk(const uint64_t *root, uint64_t va, uint64_t *pte_out, int *level_out);

/* "rwxugad" description of a PTE's flag bits, e.g. "r-x--ad" for kernel text. */
const char *sv39_perm_str(uint64_t pte, char buf[8]);

#endif
