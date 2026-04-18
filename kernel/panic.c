/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>
#include <core/drivers.h>

extern void kprintf(const char *fmt, ...);

void kernel_panic(const char *msg) {
    __asm__ volatile("cli");
    kprintf("\n*** KERNEL PANIC ***\n%s\n", msg);

    /* Print return addresses by walking the frame pointer chain */
    uint64_t rbp;
    __asm__ volatile("movq %%rbp, %0" : "=r"(rbp));
    kprintf("Stack trace:\n");
    int depth = 0;
    while (rbp && depth < 16) {
        uint64_t ret = *((uint64_t *)rbp + 1);
        kprintf("  [%d] 0x%llx\n", depth, (unsigned long long)ret);
        rbp = *(uint64_t *)rbp;
        depth++;
    }
    while (1) __asm__ volatile("hlt");
}
