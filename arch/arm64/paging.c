/* CORE Kernel — (c) CORE Project, MIT License */
/* arch/arm64/paging.c — ARMv8 VMSAv8-64 page tables (4 KB granule, 48-bit VA) */
#include <core/types.h>
#include <core/mm.h>

#define PTE_VALID    (1ULL << 0)
#define PTE_TABLE    (1ULL << 1)
#define PTE_PAGE     (3ULL << 0)    /* valid + not-table = page desc */
#define PTE_AF       (1ULL << 10)   /* access flag */
#define PTE_SH_IS    (3ULL << 8)    /* inner shareable */
#define PTE_AP_RW    (1ULL << 6)    /* EL0 read-write */
#define PTE_AP_RO    (3ULL << 6)    /* EL0 read-only */
#define PTE_UXN      (1ULL << 54)
#define PTE_PXN      (1ULL << 53)
#define PTE_ADDR_MASK 0x0000FFFFFFFFF000ULL

#define ARM64_KERNEL_BASE 0xFFFF000000000000ULL

static uint64_t *arm64_pgd; /* level-0 page table */

static uint64_t *alloc_pt(void) {
    void *page = pmm_alloc(0);
    if (!page) return NULL;
    uint64_t *pt = (uint64_t *)PHYS_TO_VIRT(page);
    for (int i = 0; i < 512; i++) pt[i] = 0;
    return pt;
}

static uint64_t *get_or_create_arm64(uint64_t *table, int idx) {
    if (!(table[idx] & PTE_VALID)) {
        uint64_t *child = alloc_pt();
        if (!child) return NULL;
        table[idx] = VIRT_TO_PHYS((uint64_t)child) | PTE_TABLE | PTE_VALID;
    }
    return (uint64_t *)PHYS_TO_VIRT(table[idx] & PTE_ADDR_MASK);
}

int arm64_map_page(uint64_t *pgd, uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    int l0 = (vaddr >> 39) & 0x1FF;
    int l1 = (vaddr >> 30) & 0x1FF;
    int l2 = (vaddr >> 21) & 0x1FF;
    int l3 = (vaddr >> 12) & 0x1FF;

    uint64_t *l1t = get_or_create_arm64(pgd, l0);
    if (!l1t) return -1;
    uint64_t *l2t = get_or_create_arm64(l1t, l1);
    if (!l2t) return -1;
    uint64_t *l3t = get_or_create_arm64(l2t, l2);
    if (!l3t) return -1;
    l3t[l3] = (paddr & PTE_ADDR_MASK) | flags | PTE_PAGE | PTE_AF | PTE_VALID;
    return 0;
}

void arm64_vmm_init(void) {
    arm64_pgd = alloc_pt();
    if (!arm64_pgd) return;

    /* Map first 64 MB to kernel virtual base */
    for (size_t i = 0; i < 16384; i++) {
        arm64_map_page(arm64_pgd,
                       ARM64_KERNEL_BASE + i * PAGE_SIZE,
                       i * PAGE_SIZE,
                       PTE_SH_IS | PTE_UXN);
    }

    /* Load TTBR1_EL1 with kernel page table */
    uint64_t pgd_phys = VIRT_TO_PHYS((uint64_t)arm64_pgd);
    __asm__ volatile("msr ttbr1_el1, %0; isb" : : "r"(pgd_phys));
}
