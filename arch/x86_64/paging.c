/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>
#include <core/mm.h>
#include <core/drivers.h>

/* 4-level paging: PML4 → PDPT → PD → PT, 4 KB pages */

#define PTE_PRESENT  (1ULL << 0)
#define PTE_WRITE    (1ULL << 1)
#define PTE_USER     (1ULL << 2)
#define PTE_HUGE     (1ULL << 7)
#define PTE_NX       (1ULL << 63)
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

static uint64_t *get_or_create(uint64_t *table, int idx, uint64_t flags) {
    if (!(table[idx] & PTE_PRESENT)) {
        void *page = pmm_alloc(0);
        if (!page) return NULL;
        uint8_t *p = (uint8_t *)PHYS_TO_VIRT(page);
        for (size_t i = 0; i < PAGE_SIZE; i++) p[i] = 0;
        table[idx] = (uint64_t)page | flags;
    }
    return (uint64_t *)PHYS_TO_VIRT(table[idx] & PTE_ADDR_MASK);
}

int vmm_map_page(uint64_t *pml4, uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    int pml4_idx = (vaddr >> 39) & 0x1FF;
    int pdpt_idx = (vaddr >> 30) & 0x1FF;
    int pd_idx   = (vaddr >> 21) & 0x1FF;
    int pt_idx   = (vaddr >> 12) & 0x1FF;

    uint64_t *pdpt = get_or_create(pml4, pml4_idx, PTE_PRESENT | PTE_WRITE | PTE_USER);
    if (!pdpt) return -1;
    uint64_t *pd = get_or_create(pdpt, pdpt_idx, PTE_PRESENT | PTE_WRITE | PTE_USER);
    if (!pd) return -1;
    uint64_t *pt = get_or_create(pd, pd_idx, PTE_PRESENT | PTE_WRITE | PTE_USER);
    if (!pt) return -1;
    pt[pt_idx] = (paddr & PTE_ADDR_MASK) | flags;
    return 0;
}

int vmm_map(uint64_t *pml4, uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    return vmm_map_page(pml4, vaddr, paddr, flags);
}

void vmm_unmap(uint64_t *pml4, uint64_t vaddr) {
    int pml4_idx = (vaddr >> 39) & 0x1FF;
    int pdpt_idx = (vaddr >> 30) & 0x1FF;
    int pd_idx   = (vaddr >> 21) & 0x1FF;
    int pt_idx   = (vaddr >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & PTE_PRESENT)) return;
    uint64_t *pdpt = (uint64_t *)PHYS_TO_VIRT(pml4[pml4_idx] & PTE_ADDR_MASK);
    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) return;
    uint64_t *pd = (uint64_t *)PHYS_TO_VIRT(pdpt[pdpt_idx] & PTE_ADDR_MASK);
    if (!(pd[pd_idx] & PTE_PRESENT)) return;
    uint64_t *pt = (uint64_t *)PHYS_TO_VIRT(pd[pd_idx] & PTE_ADDR_MASK);
    pt[pt_idx] = 0;
    __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}

static uint64_t *kernel_pml4;

static void map_kernel_range(uint64_t *pml4, uint64_t vbase, uint64_t pbase, size_t n_pages) {
    for (size_t i = 0; i < n_pages; i++) {
        vmm_map_page(pml4, vbase + i * PAGE_SIZE, pbase + i * PAGE_SIZE,
                     PTE_PRESENT | PTE_WRITE);
    }
}

void vmm_init(void) {
    /* Allocate kernel PML4 */
    kernel_pml4 = (uint64_t *)PHYS_TO_VIRT(pmm_alloc(0));
    uint8_t *p = (uint8_t *)kernel_pml4;
    for (size_t i = 0; i < PAGE_SIZE; i++) p[i] = 0;

    /* Map first 64 MB of physical RAM to higher half */
    map_kernel_range(kernel_pml4, KERNEL_BASE, 0, 16384);

    /* Identity map first 8 MB */
    map_kernel_range(kernel_pml4, 0, 0, 2048);

    /* Load new PML4 */
    uint64_t phys = VIRT_TO_PHYS((uint64_t)kernel_pml4);
    __asm__ volatile("movq %0, %%cr3" : : "r"(phys) : "memory");
}

uint64_t *vmm_get_kernel_pml4(void) {
    return kernel_pml4;
}

static void copy_pt(uint64_t *dst_pt, uint64_t *src_pt) {
    for (int i = 0; i < 512; i++) {
        if (src_pt[i] & PTE_PRESENT) {
            /* Mark COW: clear write, set dirty bit pattern */
            dst_pt[i] = (src_pt[i] & ~PTE_WRITE) | 0x200; /* bit 9 = COW marker */
            src_pt[i] = (src_pt[i] & ~PTE_WRITE) | 0x200;
        }
    }
}

static void copy_pd(uint64_t *dst_pd, uint64_t *src_pd) {
    for (int i = 0; i < 512; i++) {
        if (src_pd[i] & PTE_PRESENT) {
            void *new_pt_phys = pmm_alloc(0);
            if (!new_pt_phys) continue;
            uint64_t *new_pt = (uint64_t *)PHYS_TO_VIRT(new_pt_phys);
            uint64_t *src_pt = (uint64_t *)PHYS_TO_VIRT(src_pd[i] & PTE_ADDR_MASK);
            for (int j = 0; j < 512; j++) new_pt[j] = 0;
            copy_pt(new_pt, src_pt);
            dst_pd[i] = (uint64_t)new_pt_phys | (src_pd[i] & ~PTE_ADDR_MASK);
        }
    }
}

static void copy_pdpt(uint64_t *dst_pdpt, uint64_t *src_pdpt) {
    for (int i = 0; i < 512; i++) {
        if (src_pdpt[i] & PTE_PRESENT) {
            void *new_pd_phys = pmm_alloc(0);
            if (!new_pd_phys) continue;
            uint64_t *new_pd = (uint64_t *)PHYS_TO_VIRT(new_pd_phys);
            uint64_t *src_pd = (uint64_t *)PHYS_TO_VIRT(src_pdpt[i] & PTE_ADDR_MASK);
            for (int j = 0; j < 512; j++) new_pd[j] = 0;
            copy_pd(new_pd, src_pd);
            dst_pdpt[i] = (uint64_t)new_pd_phys | (src_pdpt[i] & ~PTE_ADDR_MASK);
        }
    }
}

uint64_t *vmm_fork(uint64_t *src_pml4) {
    void *new_pml4_phys = pmm_alloc(0);
    if (!new_pml4_phys) return NULL;
    uint64_t *new_pml4 = (uint64_t *)PHYS_TO_VIRT(new_pml4_phys);
    for (int i = 0; i < 512; i++) new_pml4[i] = 0;

    /* Copy user space (entries 0–255); share kernel (entries 256–511) */
    for (int i = 0; i < 256; i++) {
        if (src_pml4[i] & PTE_PRESENT) {
            void *new_pdpt_phys = pmm_alloc(0);
            if (!new_pdpt_phys) continue;
            uint64_t *new_pdpt = (uint64_t *)PHYS_TO_VIRT(new_pdpt_phys);
            uint64_t *src_pdpt = (uint64_t *)PHYS_TO_VIRT(src_pml4[i] & PTE_ADDR_MASK);
            for (int j = 0; j < 512; j++) new_pdpt[j] = 0;
            copy_pdpt(new_pdpt, src_pdpt);
            new_pml4[i] = (uint64_t)new_pdpt_phys | (src_pml4[i] & ~PTE_ADDR_MASK);
        }
    }
    /* Share kernel mappings */
    for (int i = 256; i < 512; i++) {
        new_pml4[i] = src_pml4[i];
    }
    return new_pml4;
}
