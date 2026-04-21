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
extern void kernel_panic(const char *msg);
extern void sched_yield(void);
extern int strcmp(const char *a, const char *b);
extern int strncmp(const char *a, const char *b, size_t n);
extern size_t strlen(const char *s);
extern char *strncpy(char *dst, const char *src, size_t n);
extern void *memset(void *dst, int c, size_t n);

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
static volatile int builtin_shell_ready = 0;

static uint64_t boot_total_ram_bytes(void) {
    uint64_t total = 0;
    for (size_t i = 0; i < mmap_count; i++) {
        if (mmap_entries[i].type != MB2_MMAP_AVAILABLE) continue;
        uint64_t base = mmap_entries[i].base;
        uint64_t len  = mmap_entries[i].length;
        if (base < 0x200000) {
            if (base + len <= 0x200000) continue;
            len -= (0x200000 - base);
            base = 0x200000;
        }
        (void)base;
        total += len;
    }
    return total;
}

static const char *boot_arch_name(void) {
#ifdef __aarch64__
    return "arm64";
#else
    return "x86_64";
#endif
}

static const char *boot_mode_name(void) {
#ifdef __aarch64__
    return "arm64 direct";
#else
    return initrd_start && initrd_size ? "multiboot2 + initrd" : "multiboot2";
#endif
}

static void read_hostname(char *buf, size_t size) {
    if (!buf || size == 0) return;

    strncpy(buf, "core", size - 1);
    buf[size - 1] = '\0';

    int fd = vfs_open("/etc/hostname", O_RDONLY, 0);
    if (fd < 0) return;

    ssize_t n = vfs_read(fd, buf, size - 1);
    vfs_close(fd);
    if (n <= 0) {
        strncpy(buf, "core", size - 1);
        buf[size - 1] = '\0';
        return;
    }

    if ((size_t)n >= size) n = (ssize_t)size - 1;
    buf[n] = '\0';
    while (n > 0) {
        char c = buf[n - 1];
        if (c != '\n' && c != '\r' && c != ' ' && c != '\t') break;
        buf[--n] = '\0';
    }
    if (buf[0] == '\0') {
        strncpy(buf, "core", size - 1);
        buf[size - 1] = '\0';
    }
}

static void print_uptime_compact(uint64_t uptime_ms) {
    uint64_t total_seconds = uptime_ms / 1000;
    uint64_t days = total_seconds / 86400;
    uint64_t hours = (total_seconds / 3600) % 24;
    uint64_t minutes = (total_seconds / 60) % 60;
    uint64_t seconds = total_seconds % 60;
    int printed = 0;

    if (days) {
        kprintf("%llud", (unsigned long long)days);
        printed = 1;
    }
    if (hours || printed) {
        kprintf(printed ? " %lluh" : "%lluh", (unsigned long long)hours);
        printed = 1;
    }
    if (minutes || printed) {
        kprintf(printed ? " %llum" : "%llum", (unsigned long long)minutes);
        printed = 1;
    }
    kprintf(printed ? " %llus" : "%llus", (unsigned long long)seconds);
}

static void builtin_fetch(void) {
    char hostname[64];
    uint64_t total_mib = boot_total_ram_bytes() / (1024ULL * 1024ULL);
    uint64_t free_mib  = pmm_free_bytes() / (1024ULL * 1024ULL);

    read_hostname(hostname, sizeof(hostname));

    kprintf("\n");
    kprintf("      .-========-.        Host: %s\n", hostname);
    kprintf("      \\\'-======-'/        OS: CORE OS\n");
    kprintf("      _|   .--.   |_       Kernel: %s\n", __core_version);
    kprintf("     ((|  / /\\ \\  |))      Arch: %s\n", boot_arch_name());
    kprintf("      \\|  \\_\\/_/  |//       Shell: kernel-console\n");
    kprintf("       \\    ____    //        Uptime: ");
    print_uptime_compact(kernel_uptime_ms());
    kprintf("\n");
    kprintf("        '-.______.-'         Memory: %llu MiB free / %llu MiB total\n",
            (unsigned long long)free_mib,
            (unsigned long long)total_mib);
    kprintf("                               Procs: %d\n", proc_count());
    kprintf("                               Boot: %s\n", boot_mode_name());
    kprintf("                               Init: built-in kernel console\n");
    kprintf("\n");
}

static void builtin_help(void) {
    kprintf("Built-in commands:\n");
    kprintf("  help            show this command list\n");
    kprintf("  fetch           show a CORE fastfetch\n");
    kprintf("  clear           clear the VGA text console\n");
    kprintf("  pwd             print current directory\n");
    kprintf("  ls [path]       list a directory\n");
    kprintf("  cd [path]       change current directory\n");
    kprintf("  cat <path>      print a file\n");
    kprintf("  echo <text>     print text\n");
    kprintf("  uname           show kernel identity\n");
    kprintf("  reboot          reboot the machine\n");
}

static char *trim_command(char *line) {
    size_t len;

    while (*line == ' ' || *line == '\t') line++;
    len = strlen(line);
    while (len > 0) {
        char c = line[len - 1];
        if (c != ' ' && c != '\t') break;
        line[--len] = '\0';
    }
    return line;
}

static char *split_command_args(char *cmd) {
    while (*cmd && *cmd != ' ' && *cmd != '\t') cmd++;
    if (*cmd == '\0') return cmd;
    *cmd++ = '\0';
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    return cmd;
}

static const char *shell_cwd(void) {
    if (current && current->cwd[0]) return current->cwd;
    return "/";
}

static int shell_resolve_path(const char *input, char *out, size_t size) {
    size_t pos = 0;
    const char *cwd = shell_cwd();

    if (!out || size < 2) return -EINVAL;
    if (!input || input[0] == '\0' || strcmp(input, ".") == 0) {
        strncpy(out, cwd, size - 1);
        out[size - 1] = '\0';
        return 0;
    }
    if (input[0] == '/') {
        strncpy(out, input, size - 1);
        out[size - 1] = '\0';
        return 0;
    }

    while (cwd[pos] && pos + 1 < size) {
        out[pos] = cwd[pos];
        pos++;
    }
    if (pos == 0) out[pos++] = '/';
    if (pos > 1 && out[pos - 1] != '/' && pos + 1 < size) out[pos++] = '/';

    for (size_t i = 0; input[i] && pos + 1 < size; i++) {
        out[pos++] = input[i];
    }
    out[pos] = '\0';
    return 0;
}

static void shell_print_file(const char *path) {
    char buf[128];
    int fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0) {
        kprintf("cat: cannot open %s\n", path);
        return;
    }

    for (;;) {
        ssize_t n = vfs_read(fd, buf, sizeof(buf));
        if (n < 0) {
            kprintf("cat: read failed for %s\n", path);
            break;
        }
        if (n == 0) break;
        for (ssize_t i = 0; i < n; i++) kprintf("%c", buf[i]);
    }
    vfs_close(fd);
}

static void shell_show_motd(void) {
    struct inode *motd = vfs_resolve("/etc/motd");
    if (!motd) return;
    shell_print_file("/etc/motd");
    kprintf("\n");
}

static void shell_cmd_pwd(void) {
    kprintf("%s\n", shell_cwd());
}

static void shell_cmd_uname(void) {
    kprintf("CORE %s %s\n", __core_version, boot_arch_name());
}

static void shell_cmd_echo(const char *args) {
    kprintf("%s\n", args && args[0] ? args : "");
}

static void shell_cmd_cd(const char *args) {
    char path[256];
    struct inode *ino;

    if (!args || args[0] == '\0') args = "/";
    if (shell_resolve_path(args, path, sizeof(path)) < 0) {
        kprintf("cd: invalid path\n");
        return;
    }

    ino = vfs_resolve(path);
    if (!ino) {
        kprintf("cd: no such directory: %s\n", args);
        return;
    }
    if (!S_ISDIR(ino->mode)) {
        kprintf("cd: not a directory: %s\n", args);
        return;
    }

    if (current) {
        strncpy(current->cwd, path, sizeof(current->cwd) - 1);
        current->cwd[sizeof(current->cwd) - 1] = '\0';
    }
}

static void shell_cmd_ls(const char *args) {
    char path[256];
    struct stat st;
    struct dirent entries[16];
    int fd;
    int first = 1;

    if (shell_resolve_path(args && args[0] ? args : ".", path, sizeof(path)) < 0) {
        kprintf("ls: invalid path\n");
        return;
    }
    if (vfs_stat(path, &st) < 0) {
        kprintf("ls: cannot access %s\n", path);
        return;
    }
    if (!S_ISDIR(st.st_mode)) {
        kprintf("%s\n", path);
        return;
    }

    fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0) {
        kprintf("ls: cannot open %s\n", path);
        return;
    }

    for (;;) {
        int n = vfs_readdir(fd, entries, ARRAY_SIZE(entries));
        if (n < 0) {
            kprintf("ls: failed to read %s\n", path);
            break;
        }
        if (n == 0) break;
        for (int i = 0; i < n; i++) {
            kprintf(first ? "%s%s" : "  %s%s",
                    entries[i].d_name,
                    entries[i].d_type == DT_DIR ? "/" : "");
            first = 0;
        }
    }
    vfs_close(fd);
    kprintf("\n");
}

static void shell_cmd_cat(const char *args) {
    char path[256];
    struct stat st;

    if (!args || args[0] == '\0') {
        kprintf("cat: missing file operand\n");
        return;
    }
    if (shell_resolve_path(args, path, sizeof(path)) < 0) {
        kprintf("cat: invalid path\n");
        return;
    }
    if (vfs_stat(path, &st) < 0) {
        kprintf("cat: cannot access %s\n", args);
        return;
    }
    if (S_ISDIR(st.st_mode)) {
        kprintf("cat: %s: is a directory\n", args);
        return;
    }
    shell_print_file(path);
    if (st.st_size == 0 || path[0] == '\0') return;
    kprintf("\n");
}

static void shell_cmd_reboot(void) {
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) null_idt = {0, 0};
    kprintf("CORE: rebooting...\n");
    __asm__ volatile("cli; lidt %0; int $3" : : "m"(null_idt));
    for (;;) __asm__ volatile("hlt");
}

static void run_builtin_command(char *line) {
    char *cmd = trim_command(line);
    char *args = split_command_args(cmd);

    if (cmd[0] == '\0') return;
    if (strcmp(cmd, "help") == 0) {
        builtin_help();
        return;
    }
    if (strcmp(cmd, "fetch") == 0) {
        builtin_fetch();
        return;
    }
    if (strcmp(cmd, "clear") == 0) {
        vga_init();
        return;
    }
    if (strcmp(cmd, "pwd") == 0) {
        shell_cmd_pwd();
        return;
    }
    if (strcmp(cmd, "uname") == 0) {
        shell_cmd_uname();
        return;
    }
    if (strcmp(cmd, "echo") == 0) {
        shell_cmd_echo(args);
        return;
    }
    if (strcmp(cmd, "cd") == 0) {
        shell_cmd_cd(args);
        return;
    }
    if (strcmp(cmd, "ls") == 0) {
        shell_cmd_ls(args);
        return;
    }
    if (strcmp(cmd, "cat") == 0) {
        shell_cmd_cat(args);
        return;
    }
    if (strcmp(cmd, "reboot") == 0) {
        shell_cmd_reboot();
        return;
    }

    kprintf("core: unknown command: %s\n", cmd);
}

static void builtin_shell(void) {
    char line[128];
    char hostname[64];

    read_hostname(hostname, sizeof(hostname));
    kprintf("CORE shell starting on %s\n", hostname);
    shell_show_motd();

    for (;;) {
        size_t len = 0;
        memset(line, 0, sizeof(line));
        kprintf("root@%s:%s# ", hostname, shell_cwd());

        for (;;) {
            char c = keyboard_getchar();

            if (c == '\r') continue;
            if (c == '\b') {
                if (len > 0) {
                    line[--len] = '\0';
                    kprintf("\b \b");
                }
                continue;
            }
            if (c == '\n') {
                kprintf("\n");
                break;
            }
            if ((uint8_t)c < ' ' || len + 1 >= sizeof(line)) continue;

            line[len++] = c;
            line[len] = '\0';
            kprintf("%c", c);
        }

        run_builtin_command(line);
    }
}

static void builtin_init(void *arg) {
    (void)arg;

#ifdef __aarch64__
    kprintf("CORE: built-in init active\n");
    for (;;) {
        timer_sleep_ms(1000);
    }
#else
    while (!builtin_shell_ready) {
        sched_yield();
    }
    builtin_shell();
#endif
}

static void ensure_bootstrap_process(void) {
    if (proc_count() > 1) return;

    kprintf("CORE: no runnable ELF init found; starting built-in init\n");
    int pid = proc_kthread(builtin_init, NULL);
    if (pid < 0) {
        kernel_panic("failed to start built-in init");
    }
    kprintf("CORE: PID %d launched (built-in init)\n", pid);
}

static void parse_mb2(uint64_t mb2_info) {
    if (!mb2_info) {
        /* No MB2 info: use a default memory map for QEMU */
#ifdef __aarch64__
        mmap_entries[0].base   = 0x40800000; /* QEMU virt: 8 MB into DRAM, above kernel */
        mmap_entries[0].length = 56 * 1024 * 1024;
#else
        mmap_entries[0].base   = 0x200000;
        mmap_entries[0].length = 30 * 1024 * 1024; /* 30 MB */
#endif
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
#ifdef __aarch64__
        mmap_entries[0].base   = 0x40800000;
        mmap_entries[0].length = 56 * 1024 * 1024;
#else
        mmap_entries[0].base   = 0x200000;
        mmap_entries[0].length = 30 * 1024 * 1024;
#endif
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
        /* Create a minimal root so the built-in init fallback has sane paths. */
        vfs_mkdir("/sbin", 0755);
        vfs_mkdir("/bin", 0755);
        vfs_mkdir("/tmp", 0755);
        proc_execve("/sbin/init", NULL, NULL);
    }

    ensure_bootstrap_process();

    /* 16. Syscall */
    syscall_init();

    /* 17. Keyboard */
    keyboard_init();

    /* 18. Selftest */
    kernel_selftest();
    builtin_shell_ready = 1;

    /* 19. Launch PID 1 (already done in initrd_mount or fallback above) */

    /* 20. Start scheduler */
    kprintf("CORE: init complete — idle\n");
    sched_start();

    /* Should never reach here */
#ifdef __aarch64__
    while (1) __asm__ volatile("wfe");
#else
    while (1) __asm__ volatile("hlt");
#endif
}
