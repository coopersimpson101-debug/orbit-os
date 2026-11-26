/* Memory management: physical pages (buddy), kernel heap, MMU. */
#ifndef ORBIT_MM_H
#define ORBIT_MM_H

#include <stddef.h>
#include <stdint.h>
#include "buddy.h"
#include "arch.h"
#include "kheap.h"

/* Higher-half linear alias of RAM at KERNEL_VA_BASE (arch.h). */
#define phys_to_virt(pa) ((void *)(uintptr_t)((uint64_t)(pa) + KERNEL_VA_BASE))
#define virt_to_phys(va) ((uint64_t)(uintptr_t)(va) - KERNEL_VA_BASE)

void  mm_init(void);
void *page_alloc(unsigned order);
void  page_free(void *p);
void *kmalloc(size_t n);
void *kzalloc(size_t n);
void  kfree(void *p);

const struct buddy *mm_buddy(void);
const struct kheap_stats *mm_heap_stats(void);

/* MMU */
void mmu_init(void);
int  mmu_enabled(void);
size_t mmu_tables_used(void);
/* Translate a VA as a kernel read would (AArch64: AT S1E1R; RV64: a
 * software Sv39 walk honouring the U bit). Returns PA or ~0 on fault. */
uint64_t mmu_translate(uint64_t va);
int mmu_selftest_higher_half(void);

#endif
