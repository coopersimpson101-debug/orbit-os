/* Memory management: physical pages (buddy), kernel heap, MMU. */
#ifndef ORBIT_MM_H
#define ORBIT_MM_H

#include <stddef.h>
#include <stdint.h>
#include "buddy.h"
#include "kheap.h"

#define KSTACK_ORDER 2 /* 16 KiB kernel stacks */

/* Higher-half linear map of RAM installed in TTBR1_EL1. */
#define KERNEL_VA_BASE 0xffff000000000000ULL
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
/* Translate a VA with AT S1E1R. Returns PA or ~0 on fault. */
uint64_t mmu_translate(uint64_t va);
int mmu_selftest_higher_half(void);

#endif
