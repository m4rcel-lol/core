/* CORE Kernel — (c) CORE Project, MIT License */
#ifndef CORE_MM_H
#define CORE_MM_H

#include <core/types.h>

#define PMM_MAX_ORDER 10
#define KERNEL_BASE   0xFFFFFFFF80000000ULL
#define PHYS_TO_VIRT(p) ((void *)((uint64_t)(p) + KERNEL_BASE))
#define VIRT_TO_PHYS(v) ((uint64_t)(v) - KERNEL_BASE)

struct mb2_mmap_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
};

#define MB2_MMAP_AVAILABLE 1

void  pmm_init(struct mb2_mmap_entry *mmap, size_t count);
void *pmm_alloc(int order);
void  pmm_free(void *addr, int order);
size_t pmm_free_bytes(void);

void  slab_init(void);
void *kmalloc(size_t size);
void  kfree(void *ptr);

#define VMM_FLAG_PRESENT  (1ULL << 0)
#define VMM_FLAG_WRITE    (1ULL << 1)
#define VMM_FLAG_USER     (1ULL << 2)
#define VMM_FLAG_ACCESSED (1ULL << 5)
#define VMM_FLAG_DIRTY    (1ULL << 6)
#define VMM_FLAG_HUGE     (1ULL << 7)
#define VMM_FLAG_NX       (1ULL << 63)

void  vmm_init(void);
int   vmm_map(uint64_t *pml4, uint64_t vaddr, uint64_t paddr, uint64_t flags);
void  vmm_unmap(uint64_t *pml4, uint64_t vaddr);
uint64_t *vmm_fork(uint64_t *pml4);
uint64_t *vmm_get_kernel_pml4(void);
int   vmm_handle_cow(uint64_t cr2, uint64_t err_code);

#endif /* CORE_MM_H */
