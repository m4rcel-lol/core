/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>
#include <core/mm.h>
#include <core/vfs.h>
#include <core/proc.h>
#include <core/drivers.h>
#include <core/syscall.h>

extern void gdt_init(void);
extern void idt_init(void);
extern void pic_init(void);
extern void vmm_init(void);
extern void slab_init(void);
extern void timer_init(void);
extern void proc_init(void);
extern void sched_init(void);
extern void sched_set_idle(struct proc *p);
extern void vfs_init(void);
extern void tmpfs_register(void);
extern void tmpfs_mount_root(void);
extern void initrd_mount(uint64_t addr, uint64_t size);
extern void syscall_init(void);
extern void keyboard_init(void);
extern void kernel_selftest(void);
extern void sched_start(void);
extern struct proc *proc_alloc(void);
extern void sched_enqueue(struct proc *p);
extern const char __core_version[];

/* Multiboot2 structures */
#define MB2_MAGIC_EXPECTED 0x36D76289U

struct mb2_tag {
    uint32_t type;
    uint32_t size;
};

struct mb2_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
};

struct mb2_tag_module {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    char     string[];
};

#define MB2_TAG_MMAP   6
#define MB2_TAG_MODULE 3
#define MB2_TAG_END    0

static struct mb2_mmap_entry mmap_entries[64];
static size_t mmap_count = 0;
static uint64_t initrd_start = 0;
static uint64_t initrd_size  = 0;

static void parse_mb2(uint64_t mb2_info) {
    if (!mb2_info) {
        /* No MB2 info: use a default memory map for QEMU */
        mmap_entries[0].base   = 0x200000;
        mmap_entries[0].length = 30 * 1024 * 1024; /* 30 MB */
        mmap_entries[0].type   = MB2_MMAP_AVAILABLE;
        mmap_count = 1;
        return;
    }
    /* Skip first 8 bytes (total size + reserved) */
    uint8_t *p = (uint8_t *)mb2_info + 8;
    uint8_t *end = (uint8_t *)mb2_info + *(uint32_t *)mb2_info;
    while (p < end) {
        struct mb2_tag *tag = (struct mb2_tag *)p;
        if (tag->type == MB2_TAG_END) break;
        if (tag->type == MB2_TAG_MMAP) {
            struct mb2_tag_mmap *mt = (struct mb2_tag_mmap *)tag;
            uint8_t *ep = (uint8_t *)mt + sizeof(struct mb2_tag_mmap);
            uint8_t *ee = (uint8_t *)mt + mt->size;
            while (ep + mt->entry_size <= ee && mmap_count < 64) {
                struct mb2_mmap_entry *e = (struct mb2_mmap_entry *)ep;
                mmap_entries[mmap_count++] = *e;
                ep += mt->entry_size;
            }
        } else if (tag->type == MB2_TAG_MODULE) {
            struct mb2_tag_module *mod = (struct mb2_tag_module *)tag;
            if (!initrd_start) {
                initrd_start = mod->mod_start;
                initrd_size  = mod->mod_end - mod->mod_start;
            }
        }
        /* Align to 8 bytes */
        p += (tag->size + 7) & ~7U;
    }
    if (mmap_count == 0) {
        mmap_entries[0].base   = 0x200000;
        mmap_entries[0].length = 30 * 1024 * 1024;
        mmap_entries[0].type   = MB2_MMAP_AVAILABLE;
        mmap_count = 1;
    }
}

static uint8_t idle_kstack[4096] __attribute__((aligned(16)));

void kmain(uint32_t magic, uint64_t mb2_info) {
    /* 1. serial_init */
    serial_init(115200);

    /* 2. vga_init */
    vga_init();

    /* 3. Banner */
    kprintf("%s booting...\n", __core_version);

    /* 4. GDT */
    gdt_init();

    /* 5. IDT */
    idt_init();

    /* 6. PIC */
    pic_init();

    /* Parse Multiboot2 */
    if (magic == MB2_MAGIC_EXPECTED) {
        parse_mb2(mb2_info);
    } else {
        parse_mb2(0);
    }

    /* 7. PMM */
    kprintf("Physical memory map:\n");
    pmm_init(mmap_entries, mmap_count);

    /* 8. VMM */
    vmm_init();

    /* 9. Slab */
    slab_init();

    /* 10. Timer */
    timer_init();

    /* 11. Proc */
    proc_init();

    /* 12. Sched */
    sched_init();
    struct proc *idle = proc_alloc();
    if (idle) {
        idle->priority = PRIO_IDLE;
        idle->state    = PROC_RUNNING;
        idle->ctx.rsp  = (uint64_t)&idle_kstack[sizeof(idle_kstack)];
        extern size_t strlen(const char *);
        extern char *strncpy(char *, const char *, size_t);
        strncpy(idle->name, "idle", PROC_NAME_LEN - 1);
        sched_set_idle(idle);
    }

    /* 13. VFS */
    vfs_init();

    /* 14. tmpfs */
    tmpfs_register();
    tmpfs_mount_root();

    /* 15. initrd */
    if (initrd_start && initrd_size) {
        initrd_mount(initrd_start, initrd_size);
    } else {
        kprintf("CORE: no initrd found\n");
        /* Create minimal /sbin directory and launch placeholder init */
        vfs_mkdir("/sbin", 0755);
        vfs_mkdir("/bin", 0755);
        vfs_mkdir("/tmp", 0755);
        proc_execve("/sbin/init", NULL, NULL);
    }

    /* 16. Syscall */
    syscall_init();

    /* 17. Keyboard */
    keyboard_init();

    /* 18. Selftest */
    kernel_selftest();

    /* 19. Launch PID 1 (already done in initrd_mount or fallback above) */

    /* 20. Start scheduler */
    kprintf("CORE: init complete — idle\n");
    sched_start();

    /* Should never reach here */
    while (1) __asm__ volatile("hlt");
}
