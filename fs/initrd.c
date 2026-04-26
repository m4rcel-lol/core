/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>
#include <core/vfs.h>
#include <core/mm.h>
#include <core/drivers.h>

extern size_t strlen(const char *);
extern int strcmp(const char *a, const char *b);
extern char *strncpy(char *, const char *, size_t);
extern void *memcpy(void *, const void *, size_t);
extern void *memset(void *, int, size_t);
extern int strncmp(const char *, const char *, size_t);
extern int proc_execve(const char *path, char *argv[], char *envp[]);
extern void kernel_panic(const char *msg);
extern void kprintf(const char *fmt, ...);

/* cpio newc format */
#define CPIO_MAGIC "070701"
#define CPIO_HEADER_SIZE 110

struct cpio_header {
    char magic[6];
    char ino[8];
    char mode[8];
    char uid[8];
    char gid[8];
    char nlink[8];
    char mtime[8];
    char filesize[8];
    char devmajor[8];
    char devminor[8];
    char rdevmajor[8];
    char rdevminor[8];
    char namesize[8];
    char check[8];
} __attribute__((packed));

static uint32_t parse_hex8(const char *s) {
    uint32_t v = 0;
    for (int i = 0; i < 8; i++) {
        char c = s[i];
        uint32_t d = (c >= '0' && c <= '9') ? (uint32_t)(c - '0') :
                     (c >= 'a' && c <= 'f') ? (uint32_t)(c - 'a' + 10) :
                     (c >= 'A' && c <= 'F') ? (uint32_t)(c - 'A' + 10) : 0;
        v = (v << 4) | d;
    }
    return v;
}

static uint32_t align4(uint32_t v) {
    return (v + 3) & ~3U;
}

static int exec_if_elf(const char *path) {
    struct inode *ino = vfs_resolve(path);
    if (!ino) return -ENOENT;

    int fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0) return fd;

    uint8_t ident[4];
    ssize_t n = vfs_read(fd, ident, sizeof(ident));
    vfs_close(fd);

    if (n != (ssize_t)sizeof(ident) ||
        ident[0] != 0x7fU || ident[1] != (uint8_t)'E' ||
        ident[2] != (uint8_t)'L' || ident[3] != (uint8_t)'F') {
        kprintf("initrd: %s is not an ELF executable; skipping\n", path);
        return -ENOEXEC;
    }

    return proc_execve(path, NULL, NULL);
}

void initrd_mount(uint64_t initrd_addr, uint64_t initrd_size) {
    const uint8_t *p = (const uint8_t *)PHYS_TO_VIRT(initrd_addr);
    const uint8_t *end = p + initrd_size;

    while (p + CPIO_HEADER_SIZE <= end) {
        struct cpio_header *hdr = (struct cpio_header *)p;
        if (strncmp(hdr->magic, CPIO_MAGIC, 6) != 0) {
            kprintf("initrd: bad magic at offset %zu\n",
                    (size_t)(p - (const uint8_t *)PHYS_TO_VIRT(initrd_addr)));
            break;
        }
        uint32_t namesize = parse_hex8(hdr->namesize);
        uint32_t filesize = parse_hex8(hdr->filesize);
        uint32_t mode     = parse_hex8(hdr->mode);

        const char *name = (const char *)(p + CPIO_HEADER_SIZE);

        /* Check for end of archive */
        if (strncmp(name, "TRAILER!!!", 10) == 0) break;

        /* Advance past header + name (aligned to 4) */
        uint32_t name_aligned = align4(CPIO_HEADER_SIZE + namesize);
        const uint8_t *file_data = p + name_aligned;

        /* Build full path */
        char full_path[256];
        full_path[0] = '/';
        strncpy(full_path + 1, name, 254);
        full_path[255] = '\0';

        /* Skip entries that start with "./" */
        const char *fname = name;
        if (fname[0] == '.' && fname[1] == '/') fname += 2;
        full_path[0] = '/';
        strncpy(full_path + 1, fname, 254);

        if (mode & S_IFDIR) {
            /* Create directory */
            if (strlen(fname) > 0) {
                vfs_mkdir(full_path, (int)(mode & 07777));
            }
        } else if (mode & S_IFREG) {
            /* Create file with content */
            int fd = vfs_open(full_path, O_CREAT | O_RDWR, (int)(mode & 07777));
            if (fd >= 0) {
                if (filesize > 0) {
                    vfs_write(fd, file_data, filesize);
                }
                vfs_close(fd);
            } else {
                kprintf("initrd: failed to create %s\n", full_path);
            }
        }

        /* Advance to next entry (aligned to 4) */
        uint32_t file_aligned = align4(filesize);
        p = file_data + file_aligned;
    }

    kprintf("initrd: mounted\n");

    /* Try to exec /sbin/init, then /bin/sh */
    if (exec_if_elf("/sbin/init") >= 0) {
        return;
    }
    if (exec_if_elf("/bin/sh") >= 0) {
        return;
    }
    /* No init found: create a minimal placeholder PID 1 */
    kprintf("initrd: no runnable ELF init found\n");
}
