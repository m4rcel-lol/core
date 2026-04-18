/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>
#include <core/vfs.h>
#include <core/mm.h>

extern size_t strlen(const char *);
extern int strcmp(const char *a, const char *b);
extern char *strncpy(char *, const char *, size_t);
extern void *memcpy(void *, const void *, size_t);
extern void *memset(void *, int, size_t);
extern void *memmove(void *, const void *, size_t);

/* Max 1024 inodes */
#define TMPFS_MAX_INODES 1024
#define TMPFS_FILE_MAX   65536   /* 64 KB max file size */

struct tmpfs_dirent {
    char                name[VFS_NAME_MAX + 1];
    struct tmpfs_inode *inode;
    struct tmpfs_dirent *next;
};

struct tmpfs_inode {
    ino_t    ino;
    mode_t   mode;
    off_t    size;
    uint8_t *data;       /* file data */
    size_t   data_cap;   /* allocated capacity */
    struct tmpfs_dirent *children; /* for directories */
    int      in_use;
    uint32_t refcount;
};

static struct tmpfs_inode tmpfs_inodes[TMPFS_MAX_INODES];

static struct vfs_ops tmpfs_ops;

static struct tmpfs_inode *tmpfs_alloc_inode(mode_t mode) {
    for (int i = 0; i < TMPFS_MAX_INODES; i++) {
        if (!tmpfs_inodes[i].in_use) {
            struct tmpfs_inode *t = &tmpfs_inodes[i];
            t->in_use   = 1;
            t->ino      = (ino_t)(i + 1);
            t->mode     = mode;
            t->size     = 0;
            t->data     = NULL;
            t->data_cap = 0;
            t->children = NULL;
            t->refcount = 1;
            return t;
        }
    }
    return NULL;
}

static struct inode *make_vfs_inode(struct tmpfs_inode *t) {
    struct inode *ino = (struct inode *)kmalloc(sizeof(struct inode));
    if (!ino) return NULL;
    ino->ino      = t->ino;
    ino->mode     = t->mode;
    ino->size     = t->size;
    ino->refcount = 1;
    ino->ops      = &tmpfs_ops;
    ino->private  = t;
    return ino;
}

static int tmpfs_open(struct inode *ino, int flags) {
    (void)ino; (void)flags; return 0;
}

static int tmpfs_close(struct inode *ino) {
    (void)ino; return 0;
}

static ssize_t tmpfs_read(struct inode *ino, void *buf, size_t n, off_t off) {
    struct tmpfs_inode *t = (struct tmpfs_inode *)ino->private;
    if (!t || !t->data) return 0;
    if (off >= t->size) return 0;
    size_t avail = (size_t)(t->size - off);
    if (n > avail) n = avail;
    memcpy(buf, t->data + off, n);
    ino->size = t->size;
    return (ssize_t)n;
}

static int tmpfs_ensure_cap(struct tmpfs_inode *t, size_t need) {
    if (need <= t->data_cap) return 0;
    size_t new_cap = t->data_cap ? t->data_cap * 2 : 4096;
    while (new_cap < need) new_cap *= 2;
    if (new_cap > TMPFS_FILE_MAX) new_cap = TMPFS_FILE_MAX;
    if (need > TMPFS_FILE_MAX) return -EFBIG;
    uint8_t *new_data = (uint8_t *)kmalloc(new_cap);
    if (!new_data) return -ENOMEM;
    memset(new_data, 0, new_cap);
    if (t->data) {
        memcpy(new_data, t->data, (size_t)t->size);
        kfree(t->data);
    }
    t->data     = new_data;
    t->data_cap = new_cap;
    return 0;
}

static ssize_t tmpfs_write(struct inode *ino, const void *buf, size_t n, off_t off) {
    struct tmpfs_inode *t = (struct tmpfs_inode *)ino->private;
    if (!t) return -EBADF;
    size_t end = (size_t)off + n;
    int r = tmpfs_ensure_cap(t, end);
    if (r < 0) return (ssize_t)r;
    memcpy(t->data + off, buf, n);
    if ((off_t)end > t->size) {
        t->size  = (off_t)end;
        ino->size = t->size;
    }
    return (ssize_t)n;
}

static int tmpfs_readdir(struct inode *ino, struct dirent *buf, size_t count, off_t off) {
    struct tmpfs_inode *t = (struct tmpfs_inode *)ino->private;
    if (!t || !S_ISDIR(t->mode)) return -ENOTDIR;
    struct tmpfs_dirent *de = t->children;
    off_t idx = 0;
    size_t filled = 0;
    while (de && filled < count) {
        if (idx >= off) {
            strncpy(buf[filled].d_name, de->name, VFS_NAME_MAX);
            buf[filled].d_ino    = de->inode ? de->inode->ino : 0;
            buf[filled].d_type   = de->inode && S_ISDIR(de->inode->mode) ? DT_DIR : DT_REG;
            buf[filled].d_reclen = sizeof(struct dirent);
            filled++;
        }
        idx++;
        de = de->next;
    }
    return (int)filled;
}

static int tmpfs_stat(struct inode *ino, struct stat *st) {
    struct tmpfs_inode *t = (struct tmpfs_inode *)ino->private;
    if (!t) return -EBADF;
    memset(st, 0, sizeof(*st));
    st->st_ino     = t->ino;
    st->st_mode    = t->mode;
    st->st_size    = t->size;
    st->st_blksize = 4096;
    st->st_blocks  = (t->size + 511) / 512;
    st->st_nlink   = 1;
    return 0;
}

static int tmpfs_mkdir(struct inode *parent, const char *name, int mode) {
    struct tmpfs_inode *pt = (struct tmpfs_inode *)parent->private;
    if (!pt || !S_ISDIR(pt->mode)) return -ENOTDIR;
    /* Check if exists */
    struct tmpfs_dirent *de = pt->children;
    while (de) {
        if (strcmp(de->name, name) == 0) return -EEXIST;
        de = de->next;
    }
    struct tmpfs_inode *child = tmpfs_alloc_inode((mode_t)mode | S_IFDIR);
    if (!child) return -ENOMEM;
    struct tmpfs_dirent *entry = (struct tmpfs_dirent *)kmalloc(sizeof(struct tmpfs_dirent));
    if (!entry) return -ENOMEM;
    strncpy(entry->name, name, VFS_NAME_MAX);
    entry->inode = child;
    entry->next  = pt->children;
    pt->children = entry;
    return 0;
}

static int tmpfs_unlink(struct inode *parent, const char *name) {
    struct tmpfs_inode *pt = (struct tmpfs_inode *)parent->private;
    if (!pt || !S_ISDIR(pt->mode)) return -ENOTDIR;
    struct tmpfs_dirent **prev = &pt->children;
    struct tmpfs_dirent *de   = pt->children;
    while (de) {
        if (strcmp(de->name, name) == 0) {
            *prev = de->next;
            if (de->inode) {
                if (de->inode->data) kfree(de->inode->data);
                de->inode->in_use = 0;
            }
            kfree(de);
            return 0;
        }
        prev = &de->next;
        de   = de->next;
    }
    return -ENOENT;
}

static int tmpfs_rename(struct inode *old_dir, const char *old_name,
                        struct inode *new_dir, const char *new_name) {
    struct tmpfs_inode *old_pt = (struct tmpfs_inode *)old_dir->private;
    struct tmpfs_inode *new_pt = (struct tmpfs_inode *)new_dir->private;
    if (!old_pt || !new_pt) return -EBADF;
    /* Find and remove from old */
    struct tmpfs_dirent **prev = &old_pt->children;
    struct tmpfs_dirent *de   = old_pt->children;
    while (de) {
        if (strcmp(de->name, old_name) == 0) {
            *prev = de->next;
            /* Insert into new parent */
            strncpy(de->name, new_name, VFS_NAME_MAX);
            de->next = new_pt->children;
            new_pt->children = de;
            return 0;
        }
        prev = &de->next;
        de   = de->next;
    }
    return -ENOENT;
}

static struct inode *tmpfs_lookup(struct inode *parent, const char *name) {
    struct tmpfs_inode *pt = (struct tmpfs_inode *)parent->private;
    if (!pt || !S_ISDIR(pt->mode)) return NULL;
    struct tmpfs_dirent *de = pt->children;
    while (de) {
        if (strcmp(de->name, name) == 0) {
            if (!de->inode) return NULL;
            return make_vfs_inode(de->inode);
        }
        de = de->next;
    }
    return NULL;
}

static struct inode *tmpfs_create(struct inode *parent, const char *name, int mode) {
    struct tmpfs_inode *pt = (struct tmpfs_inode *)parent->private;
    if (!pt || !S_ISDIR(pt->mode)) return NULL;
    struct tmpfs_inode *child = tmpfs_alloc_inode((mode_t)mode);
    if (!child) return NULL;
    struct tmpfs_dirent *entry = (struct tmpfs_dirent *)kmalloc(sizeof(struct tmpfs_dirent));
    if (!entry) return NULL;
    strncpy(entry->name, name, VFS_NAME_MAX);
    entry->inode = child;
    entry->next  = pt->children;
    pt->children = entry;
    return make_vfs_inode(child);
}

static struct vfs_ops tmpfs_ops = {
    .open    = tmpfs_open,
    .close   = tmpfs_close,
    .read    = tmpfs_read,
    .write   = tmpfs_write,
    .readdir = tmpfs_readdir,
    .stat    = tmpfs_stat,
    .mkdir   = tmpfs_mkdir,
    .unlink  = tmpfs_unlink,
    .rename  = tmpfs_rename,
    .lookup  = tmpfs_lookup,
    .create  = tmpfs_create,
};

static struct inode *tmpfs_mount(void *data) {
    (void)data;
    struct tmpfs_inode *root = tmpfs_alloc_inode(S_IFDIR | 0755);
    if (!root) return NULL;
    return make_vfs_inode(root);
}

void tmpfs_register(void) {
    vfs_register("tmpfs", &tmpfs_ops, tmpfs_mount, NULL);
}

void tmpfs_mount_root(void) {
    vfs_mount("/", "tmpfs", NULL);
}
