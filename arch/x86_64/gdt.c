/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>

#define GDT_ENTRIES 7

/* GDT entry bits */
#define GDT_PRESENT    (1ULL << 47)
#define GDT_DPL0       (0ULL << 45)
#define GDT_DPL3       (3ULL << 45)
#define GDT_SEG        (1ULL << 44)
#define GDT_EXEC       (1ULL << 43)
#define GDT_RW         (1ULL << 41)
#define GDT_GRAN       (1ULL << 55)
#define GDT_32BIT      (1ULL << 54)
#define GDT_64BIT      (1ULL << 53)

#define GDT_KERNEL_CODE (GDT_PRESENT | GDT_DPL0 | GDT_SEG | GDT_EXEC | GDT_RW | GDT_64BIT)
#define GDT_KERNEL_DATA (GDT_PRESENT | GDT_DPL0 | GDT_SEG | GDT_RW)
#define GDT_USER_CODE   (GDT_PRESENT | GDT_DPL3 | GDT_SEG | GDT_EXEC | GDT_RW | GDT_64BIT)
#define GDT_USER_DATA   (GDT_PRESENT | GDT_DPL3 | GDT_SEG | GDT_RW)

struct tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static uint64_t gdt[GDT_ENTRIES];
static struct tss kernel_tss;
static struct gdt_ptr gdt_ptr;

static void gdt_set_entry(int idx, uint64_t entry) {
    gdt[idx] = entry;
}

static void gdt_set_tss(int idx, uint64_t base, uint32_t limit) {
    /* TSS descriptor is 16 bytes (two consecutive GDT slots) */
    uint64_t low = 0, high = 0;
    low  = (uint64_t)(limit & 0xFFFF);
    low |= ((base & 0xFFFFFF) << 16);
    low |= ((uint64_t)0x89 << 40);       /* type = available TSS */
    low |= (((uint64_t)(limit >> 16) & 0xF) << 48);
    low |= (((base >> 24) & 0xFF) << 56);
    high = (base >> 32) & 0xFFFFFFFF;
    gdt[idx]     = low;
    gdt[idx + 1] = high;
}

static uint8_t tss_stack[8192] __attribute__((aligned(16)));

void gdt_init(void) {
    gdt_set_entry(0, 0);                       /* null */
    gdt_set_entry(1, GDT_KERNEL_CODE);         /* kernel code  (sel 0x08) */
    gdt_set_entry(2, GDT_KERNEL_DATA);         /* kernel data  (sel 0x10) */
    gdt_set_entry(3, GDT_USER_CODE);           /* user code    (sel 0x18) */
    gdt_set_entry(4, GDT_USER_DATA);           /* user data    (sel 0x20) */
    gdt_set_tss(5, (uint64_t)&kernel_tss, sizeof(kernel_tss) - 1);

    /* Set RSP0 to kernel stack */
    kernel_tss.rsp0 = (uint64_t)&tss_stack[sizeof(tss_stack)];
    kernel_tss.iopb_offset = sizeof(struct tss);

    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base  = (uint64_t)gdt;

    __asm__ volatile(
        "lgdt %0\n\t"
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "xorw %%ax, %%ax\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        : : "m"(gdt_ptr) : "rax", "memory"
    );

    /* Load TSS selector (index 5, selector = 5*8 = 0x28) */
    __asm__ volatile("ltr %0" : : "r"((uint16_t)0x28));
}

void gdt_set_kernel_stack(uint64_t rsp0) {
    kernel_tss.rsp0 = rsp0;
}
