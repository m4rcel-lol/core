/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>
#include <core/mm.h>
#include <core/drivers.h>

/* ===== Physical Memory Manager — Buddy Allocator ===== */

#define PMM_MAX_ORDER 10
#define MAX_PAGES     (1024 * 1024)   /* support up to 4 GB / 4 KB */
#define BUDDY_MAX_BLOCKS (MAX_PAGES)

struct free_block {
    struct free_block *next;
};

static struct free_block *free_list[PMM_MAX_ORDER + 1];
static size_t total_free_pages = 0;

/* Bitmap to track which pages are free (for buddy coalescing) */
static uint8_t page_bitmap[MAX_PAGES / 8];

static void page_bitmap_set(size_t pfn)   { page_bitmap[pfn/8] |=  (uint8_t)(1u << (pfn%8)); }
static void page_bitmap_clear(size_t pfn) { page_bitmap[pfn/8] &= (uint8_t)~(1u << (pfn%8)); }
static int  page_bitmap_get(size_t pfn)   { return (page_bitmap[pfn/8] >> (pfn%8)) & 1; }

static void pmm_free_range(uint64_t base, uint64_t length) {
    uint64_t start = ALIGN_UP(base, PAGE_SIZE);
    uint64_t end   = ALIGN_DOWN(base + length, PAGE_SIZE);
    if (start >= end) return;

    uint64_t addr = start;
    while (addr < end) {
        int order = PMM_MAX_ORDER;
        /* Find largest order that fits and is aligned */
        while (order > 0) {
            uint64_t block_size = (uint64_t)PAGE_SIZE << order;
            if ((addr & (block_size - 1)) == 0 && addr + block_size <= end) break;
            order--;
        }
        struct free_block *blk = (struct free_block *)PHYS_TO_VIRT(addr);
        blk->next = free_list[order];
        free_list[order] = blk;
        size_t pfn = (size_t)(addr >> PAGE_SHIFT);
        page_bitmap_set(pfn);
        total_free_pages += (size_t)(1 << order);
        addr += (uint64_t)PAGE_SIZE << order;
    }
}

void pmm_init(struct mb2_mmap_entry *mmap, size_t count) {
    for (int i = 0; i <= PMM_MAX_ORDER; i++) free_list[i] = NULL;
    for (size_t i = 0; i < sizeof(page_bitmap); i++) page_bitmap[i] = 0;
    total_free_pages = 0;

    for (size_t i = 0; i < count; i++) {
        if (mmap[i].type == MB2_MMAP_AVAILABLE) {
            uint64_t base = mmap[i].base;
            uint64_t len  = mmap[i].length;
            /* Skip first 2 MB (kernel, VGA, etc.) */
            if (base < 0x200000) {
                if (base + len <= 0x200000) continue;
                len -= (0x200000 - base);
                base = 0x200000;
            }
            kprintf("  [MEM] 0x%llx - 0x%llx (%llu KB)\n",
                    (unsigned long long)base,
                    (unsigned long long)(base + len),
                    (unsigned long long)(len / 1024));
            pmm_free_range(base, len);
        }
    }
    kprintf("PMM: %zu pages free (%zu MB)\n",
            total_free_pages, total_free_pages * PAGE_SIZE / (1024 * 1024));
}

void *pmm_alloc(int order) {
    if (order < 0 || order > PMM_MAX_ORDER) return NULL;
    /* Find a free block at or above the requested order */
    for (int o = order; o <= PMM_MAX_ORDER; o++) {
        if (free_list[o]) {
            struct free_block *blk = free_list[o];
            free_list[o] = blk->next;
            uint64_t addr = VIRT_TO_PHYS((uint64_t)blk);
            size_t pfn = (size_t)(addr >> PAGE_SHIFT);
            page_bitmap_clear(pfn);
            total_free_pages -= (size_t)(1 << o);
            /* Split excess blocks back into free lists */
            while (o > order) {
                o--;
                uint64_t buddy_addr = addr + ((uint64_t)PAGE_SIZE << o);
                struct free_block *buddy = (struct free_block *)PHYS_TO_VIRT(buddy_addr);
                buddy->next = free_list[o];
                free_list[o] = buddy;
                size_t bpfn = (size_t)(buddy_addr >> PAGE_SHIFT);
                page_bitmap_set(bpfn);
                total_free_pages += (size_t)(1 << o);
            }
            return (void *)addr;
        }
    }
    return NULL;
}

void pmm_free(void *addr, int order) {
    if (!addr || order < 0 || order > PMM_MAX_ORDER) return;
    uint64_t paddr = (uint64_t)addr;
    /* Coalesce with buddy */
    while (order < PMM_MAX_ORDER) {
        uint64_t buddy_addr = paddr ^ ((uint64_t)PAGE_SIZE << order);
        size_t bpfn = (size_t)(buddy_addr >> PAGE_SHIFT);
        if (!page_bitmap_get(bpfn)) break;
        /* Remove buddy from free list */
        struct free_block **prev = &free_list[order];
        struct free_block *cur = free_list[order];
        while (cur) {
            if (VIRT_TO_PHYS((uint64_t)cur) == buddy_addr) {
                *prev = cur->next;
                break;
            }
            prev = &cur->next;
            cur = cur->next;
        }
        if (!cur) break;
        page_bitmap_clear(bpfn);
        total_free_pages -= (size_t)(1 << order);
        paddr = (paddr < buddy_addr) ? paddr : buddy_addr;
        order++;
    }
    struct free_block *blk = (struct free_block *)PHYS_TO_VIRT(paddr);
    blk->next = free_list[order];
    free_list[order] = blk;
    size_t pfn = (size_t)(paddr >> PAGE_SHIFT);
    page_bitmap_set(pfn);
    total_free_pages += (size_t)(1 << order);
}

size_t pmm_free_bytes(void) {
    return total_free_pages * PAGE_SIZE;
}

/* ===== Slab Allocator ===== */

static const size_t slab_sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048};
#define SLAB_CACHE_COUNT  9

struct slab_obj {
    struct slab_obj *next;
};

struct slab_cache {
    size_t          obj_size;
    struct slab_obj *free_list;
};

static struct slab_cache slab_caches[SLAB_CACHE_COUNT];

static void slab_refill(struct slab_cache *cache) {
    void *page_phys = pmm_alloc(0);
    if (!page_phys) return;
    uint8_t *page = (uint8_t *)PHYS_TO_VIRT(page_phys);
    size_t n = PAGE_SIZE / cache->obj_size;
    for (size_t i = 0; i < n; i++) {
        struct slab_obj *obj = (struct slab_obj *)(page + i * cache->obj_size);
        obj->next = cache->free_list;
        cache->free_list = obj;
    }
}

void slab_init(void) {
    for (int i = 0; i < SLAB_CACHE_COUNT; i++) {
        slab_caches[i].obj_size  = slab_sizes[i];
        slab_caches[i].free_list = NULL;
        slab_refill(&slab_caches[i]);
    }
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    for (int i = 0; i < SLAB_CACHE_COUNT; i++) {
        if (size <= slab_caches[i].obj_size) {
            struct slab_cache *cache = &slab_caches[i];
            if (!cache->free_list) slab_refill(cache);
            if (!cache->free_list) return NULL;
            struct slab_obj *obj = cache->free_list;
            cache->free_list = obj->next;
            return (void *)obj;
        }
    }
    /* Large allocation: use buddy allocator directly */
    int order = 0;
    size_t needed = size;
    while (((size_t)PAGE_SIZE << order) < needed && order < PMM_MAX_ORDER) order++;
    return PHYS_TO_VIRT(pmm_alloc(order));
}

void kfree(void *ptr) {
    if (!ptr) return;
    /* Determine which slab cache owns this pointer by alignment and size */
    uint64_t vaddr = (uint64_t)ptr;
    uint64_t page_start = vaddr & ~(PAGE_SIZE - 1);
    size_t offset = (size_t)(vaddr - page_start);
    for (int i = 0; i < SLAB_CACHE_COUNT; i++) {
        size_t obj_size = slab_caches[i].obj_size;
        if (offset % obj_size == 0 && offset + obj_size <= PAGE_SIZE) {
            struct slab_obj *obj = (struct slab_obj *)ptr;
            obj->next = slab_caches[i].free_list;
            slab_caches[i].free_list = obj;
            return;
        }
    }
    /* Large allocation: return to buddy */
    pmm_free((void *)VIRT_TO_PHYS(vaddr), 0);
}
