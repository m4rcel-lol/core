/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>
#include <core/mm.h>
#include <core/drivers.h>

/* vmm.c — Virtual memory manager (higher-level than arch paging) */

/* vmm_map and vmm_unmap are implemented in arch/x86_64/paging.c.
 * This file provides process-level virtual memory operations.
 */

extern int vmm_map(uint64_t *pml4, uint64_t vaddr, uint64_t paddr, uint64_t flags);
extern void vmm_unmap(uint64_t *pml4, uint64_t vaddr);
extern uint64_t *vmm_fork(uint64_t *pml4);
extern uint64_t *vmm_get_kernel_pml4(void);
extern void vmm_init(void);

#define USER_HEAP_START  0x0000000010000000ULL
#define USER_MMAP_START  0x0000000040000000ULL
#define USER_STACK_TOP   0x0000800000000000ULL

struct vmm_region {
    uint64_t start;
    uint64_t end;
    uint64_t flags;
    int      in_use;
};

#define VMM_REGION_MAX 64
static struct vmm_region vmm_regions[VMM_REGION_MAX];
static uint64_t mmap_cursor = USER_MMAP_START;

void vmm_region_init(void) {
    for (int i = 0; i < VMM_REGION_MAX; i++) vmm_regions[i].in_use = 0;
    mmap_cursor = USER_MMAP_START;
}

void *sys_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
    (void)addr; (void)prot; (void)flags; (void)fd; (void)off;
    if (!len) return (void *)-1;
    len = ALIGN_UP(len, PAGE_SIZE);

    uint64_t *pml4 = vmm_get_kernel_pml4();
    uint64_t vaddr = mmap_cursor;
    mmap_cursor += len;

    uint64_t map_flags = VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER;
    for (size_t i = 0; i < len; i += PAGE_SIZE) {
        void *page = pmm_alloc(0);
        if (!page) return (void *)-1;
        uint8_t *p = (uint8_t *)PHYS_TO_VIRT(page);
        for (size_t j = 0; j < PAGE_SIZE; j++) p[j] = 0;
        vmm_map(pml4, vaddr + i, (uint64_t)page, map_flags);
    }
    return (void *)vaddr;
}

int sys_munmap(void *addr, size_t len) {
    uint64_t vaddr = (uint64_t)addr;
    uint64_t *pml4 = vmm_get_kernel_pml4();
    len = ALIGN_UP(len, PAGE_SIZE);
    for (size_t i = 0; i < len; i += PAGE_SIZE) {
        vmm_unmap(pml4, vaddr + i);
    }
    return 0;
}

void *sys_brk(void *addr) {
    /* Simplified brk: treat as mmap */
    static uint64_t heap_top = USER_HEAP_START;
    if (!addr || (uint64_t)addr <= heap_top) return (void *)heap_top;
    uint64_t new_top = ALIGN_UP((uint64_t)addr, PAGE_SIZE);
    uint64_t *pml4 = vmm_get_kernel_pml4();
    uint64_t map_flags = VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER;
    while (heap_top < new_top) {
        void *page = pmm_alloc(0);
        if (!page) return (void *)heap_top;
        uint8_t *p = (uint8_t *)PHYS_TO_VIRT(page);
        for (size_t i = 0; i < PAGE_SIZE; i++) p[i] = 0;
        vmm_map(pml4, heap_top, (uint64_t)page, map_flags);
        heap_top += PAGE_SIZE;
    }
    return (void *)heap_top;
}
