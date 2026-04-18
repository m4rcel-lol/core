/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>
#include <core/vfs.h>
#include <core/proc.h>
#include <core/mm.h>
#include <core/drivers.h>

extern size_t strlen(const char *);
extern int strcmp(const char *a, const char *b);
extern char *strncpy(char *, const char *, size_t);
extern void *memset(void *, int, size_t);
extern int strncmp(const char *, const char *, size_t);

/* Mount table */
static struct mount_entry mounts[VFS_MOUNT_MAX];
static int mount_count = 0;

/* File descriptor table */
static struct file_desc fd_table[VFS_FD_MAX];

/* Registered filesystem drivers */
struct fs_driver {
    char name[32];
    struct vfs_ops *ops;
    struct inode *(*mount_fn)(void *data);
};
#define FS_DRIVER_MAX 8
static struct fs_driver fs_drivers[FS_DRIVER_MAX];
static int fs_driver_count = 0;

void vfs_init(void) {
    for (int i = 0; i < VFS_FD_MAX; i++) {
        fd_table[i].inode    = NULL;
        fd_table[i].offset   = 0;
        fd_table[i].flags    = 0;
        fd_table[i].refcount = 0;
        fd_table[i].is_pipe  = 0;
        fd_table[i].pipe_ptr = NULL;
    }
    mount_count   = 0;
    fs_driver_count = 0;
}

int vfs_register(const char *name, struct vfs_ops *ops,
                 struct inode *(*mount_fn)(void *data), void *data) {
    (void)data;
    if (fs_driver_count >= FS_DRIVER_MAX) return -1;
    strncpy(fs_drivers[fs_driver_count].name, name, 31);
    fs_drivers[fs_driver_count].ops      = ops;
    fs_drivers[fs_driver_count].mount_fn = mount_fn;
    fs_driver_count++;
    return 0;
}

int vfs_mount(const char *path, const char *fstype, void *data) {
    if (mount_count >= VFS_MOUNT_MAX) return -ENFILE;
    /* Find driver */
    for (int i = 0; i < fs_driver_count; i++) {
        if (strcmp(fs_drivers[i].name, fstype) == 0) {
            struct inode *root = fs_drivers[i].mount_fn(data);
            if (!root) return -ENOMEM;
            strncpy(mounts[mount_count].path, path, VFS_NAME_MAX);
            mounts[mount_count].root = root;
            strncpy(mounts[mount_count].fstype, fstype, 31);
            mount_count++;
            return 0;
        }
    }
    return -EINVAL;
}

/* Path resolution helpers */
static struct inode *mount_root(void) {
    if (mount_count == 0) return NULL;
    return mounts[0].root; /* First mount is "/" */
}

struct inode *vfs_root_inode(void) {
    return mount_root();
}

struct inode *vfs_resolve(const char *path) {
    if (!path || path[0] != '/') return NULL;
    struct inode *cur = mount_root();
    if (!cur) return NULL;
    if (path[1] == '\0') return cur;

    const char *p = path + 1;
    while (*p) {
        /* Extract next component */
        char component[VFS_NAME_MAX + 1];
        int len = 0;
        while (*p && *p != '/') {
            if (len < VFS_NAME_MAX) component[len++] = *p;
            p++;
        }
        component[len] = '\0';
        if (*p == '/') p++;
        if (len == 0) continue;
        if (strcmp(component, ".") == 0) continue;
        if (!cur->ops || !cur->ops->lookup) return NULL;
        cur = cur->ops->lookup(cur, component);
        if (!cur) return NULL;
    }
    return cur;
}

struct inode *vfs_resolve_parent(const char *path, char *name_out) {
    if (!path || path[0] != '/') return NULL;
    /* Find last slash */
    size_t len = strlen(path);
    size_t last = len;
    while (last > 0 && path[last - 1] != '/') last--;
    /* Parent path is path[0..last-1] */
    char parent_path[256];
    if (last <= 1) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        size_t plen = last - 1;
        if (plen >= 255) plen = 254;
        for (size_t i = 0; i < plen; i++) parent_path[i] = path[i];
        parent_path[plen] = '\0';
    }
    if (name_out) strncpy(name_out, path + last, VFS_NAME_MAX);
    return vfs_resolve(parent_path);
}

int vfs_alloc_fd(struct inode *ino, int flags) {
    for (int i = 3; i < VFS_FD_MAX; i++) {   /* 0,1,2 reserved */
        if (!fd_table[i].inode && !fd_table[i].is_pipe) {
            fd_table[i].inode    = ino;
            fd_table[i].offset   = 0;
            fd_table[i].flags    = flags;
            fd_table[i].refcount = 1;
            fd_table[i].is_pipe  = 0;
            fd_table[i].pipe_ptr = NULL;
            return i;
        }
    }
    return -EMFILE;
}

int vfs_alloc_fd_pipe(void *pipe_ptr, int flags) {
    for (int i = 3; i < VFS_FD_MAX; i++) {
        if (!fd_table[i].inode && !fd_table[i].is_pipe) {
            fd_table[i].inode    = NULL;
            fd_table[i].offset   = 0;
            fd_table[i].flags    = flags;
            fd_table[i].refcount = 1;
            fd_table[i].is_pipe  = 1;
            fd_table[i].pipe_ptr = pipe_ptr;
            return i;
        }
    }
    return -EMFILE;
}

struct file_desc *vfs_get_fd(int fd) {
    if (fd < 0 || fd >= VFS_FD_MAX) return NULL;
    if (!fd_table[fd].inode && !fd_table[fd].is_pipe) return NULL;
    return &fd_table[fd];
}

int vfs_open(const char *path, int flags, int mode) {
    struct inode *ino = vfs_resolve(path);
    if (!ino) {
        if (!(flags & O_CREAT)) return -ENOENT;
        /* Create file */
        char name[VFS_NAME_MAX + 1];
        struct inode *parent = vfs_resolve_parent(path, name);
        if (!parent) return -ENOENT;
        if (!parent->ops || !parent->ops->create) return -ENOSYS;
        ino = parent->ops->create(parent, name, (mode_t)mode | S_IFREG);
        if (!ino) return -ENOMEM;
    } else if (flags & O_TRUNC) {
        if (ino->ops && ino->ops->write) {
            ino->size = 0;
        }
    }
    if (ino->ops && ino->ops->open) ino->ops->open(ino, flags);
    return vfs_alloc_fd(ino, flags);
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_FD_MAX) return -EBADF;
    struct file_desc *f = &fd_table[fd];
    if (!f->inode && !f->is_pipe) return -EBADF;
    f->refcount--;
    if (f->refcount <= 0) {
        if (f->inode && f->inode->ops && f->inode->ops->close)
            f->inode->ops->close(f->inode);
        f->inode    = NULL;
        f->is_pipe  = 0;
        f->pipe_ptr = NULL;
        f->refcount = 0;
    }
    return 0;
}

ssize_t vfs_read(int fd, void *buf, size_t n) {
    if (fd < 0 || fd >= VFS_FD_MAX) return -EBADF;
    struct file_desc *f = &fd_table[fd];
    if (!f->inode) return -EBADF;
    if (!f->inode->ops || !f->inode->ops->read) return -ENOSYS;
    ssize_t r = f->inode->ops->read(f->inode, buf, n, f->offset);
    if (r > 0) f->offset += r;
    return r;
}

ssize_t vfs_write(int fd, const void *buf, size_t n) {
    if (fd < 0 || fd >= VFS_FD_MAX) return -EBADF;
    struct file_desc *f = &fd_table[fd];
    if (!f->inode) return -EBADF;
    if (!f->inode->ops || !f->inode->ops->write) return -ENOSYS;
    ssize_t r = f->inode->ops->write(f->inode, buf, n, f->offset);
    if (r > 0) f->offset += r;
    return r;
}

off_t vfs_lseek(int fd, off_t off, int whence) {
    if (fd < 0 || fd >= VFS_FD_MAX) return -EBADF;
    struct file_desc *f = &fd_table[fd];
    if (!f->inode) return -EBADF;
    switch (whence) {
    case SEEK_SET: f->offset = off; break;
    case SEEK_CUR: f->offset += off; break;
    case SEEK_END: f->offset = f->inode->size + off; break;
    default: return -EINVAL;
    }
    if (f->offset < 0) f->offset = 0;
    return f->offset;
}

int vfs_stat(const char *path, struct stat *st) {
    struct inode *ino = vfs_resolve(path);
    if (!ino) return -ENOENT;
    if (!ino->ops || !ino->ops->stat) return -ENOSYS;
    return ino->ops->stat(ino, st);
}

int vfs_fstat(int fd, struct stat *st) {
    if (fd < 0 || fd >= VFS_FD_MAX) return -EBADF;
    struct file_desc *f = &fd_table[fd];
    if (!f->inode) return -EBADF;
    if (!f->inode->ops || !f->inode->ops->stat) return -ENOSYS;
    return f->inode->ops->stat(f->inode, st);
}

int vfs_mkdir(const char *path, int mode) {
    char name[VFS_NAME_MAX + 1];
    struct inode *parent = vfs_resolve_parent(path, name);
    if (!parent) return -ENOENT;
    if (!parent->ops || !parent->ops->mkdir) return -ENOSYS;
    return parent->ops->mkdir(parent, name, mode);
}

int vfs_unlink(const char *path) {
    char name[VFS_NAME_MAX + 1];
    struct inode *parent = vfs_resolve_parent(path, name);
    if (!parent) return -ENOENT;
    if (!parent->ops || !parent->ops->unlink) return -ENOSYS;
    return parent->ops->unlink(parent, name);
}

int vfs_rename(const char *old, const char *new_path) {
    char old_name[VFS_NAME_MAX + 1], new_name[VFS_NAME_MAX + 1];
    struct inode *old_parent = vfs_resolve_parent(old, old_name);
    struct inode *new_parent = vfs_resolve_parent(new_path, new_name);
    if (!old_parent || !new_parent) return -ENOENT;
    if (!old_parent->ops || !old_parent->ops->rename) return -ENOSYS;
    return old_parent->ops->rename(old_parent, old_name, new_parent, new_name);
}

int vfs_readdir(int fd, struct dirent *buf, size_t count) {
    if (fd < 0 || fd >= VFS_FD_MAX) return -EBADF;
    struct file_desc *f = &fd_table[fd];
    if (!f->inode) return -EBADF;
    if (!f->inode->ops || !f->inode->ops->readdir) return -ENOSYS;
    int r = f->inode->ops->readdir(f->inode, buf, count, f->offset);
    if (r > 0) f->offset += r;
    return r;
}

int vfs_dup(int fd) {
    if (fd < 0 || fd >= VFS_FD_MAX) return -EBADF;
    struct file_desc *f = &fd_table[fd];
    if (!f->inode && !f->is_pipe) return -EBADF;
    int new_fd = vfs_alloc_fd(f->inode, f->flags);
    if (new_fd < 0) return new_fd;
    fd_table[new_fd] = fd_table[fd];
    fd_table[new_fd].refcount = 1;
    return new_fd;
}

int vfs_dup2(int old, int new_fd) {
    if (old < 0 || old >= VFS_FD_MAX) return -EBADF;
    if (new_fd < 0 || new_fd >= VFS_FD_MAX) return -EBADF;
    if (old == new_fd) return new_fd;
    if (fd_table[new_fd].inode || fd_table[new_fd].is_pipe) vfs_close(new_fd);
    fd_table[new_fd] = fd_table[old];
    fd_table[new_fd].refcount = 1;
    return new_fd;
}

/*
 * vfs_dupfd — duplicate fd to the lowest available file descriptor >= minfd.
 * Like dup(2) but starts the search at minfd (F_DUPFD semantics).
 * The new descriptor inherits the file position but NOT the O_CLOEXEC flag.
 */
int vfs_dupfd(int fd, int minfd) {
    if (fd < 0 || fd >= VFS_FD_MAX) return -EBADF;
    struct file_desc *f = &fd_table[fd];
    if (!f->inode && !f->is_pipe) return -EBADF;
    if (minfd < 3) minfd = 3;   /* 0, 1, 2 are reserved for stdin/stdout/stderr */
    for (int i = minfd; i < VFS_FD_MAX; i++) {
        if (!fd_table[i].inode && !fd_table[i].is_pipe) {
            fd_table[i]          = *f;
            fd_table[i].refcount = 1;
            fd_table[i].flags   &= ~O_CLOEXEC; /* F_DUPFD always clears CLOEXEC */
            return i;
        }
    }
    return -EMFILE;
}

int vfs_ftruncate(int fd, off_t length) {
    if (fd < 0 || fd >= VFS_FD_MAX) return -EBADF;
    struct file_desc *f = &fd_table[fd];
    if (!f->inode) return -EBADF;
    if (!f->inode->ops || !f->inode->ops->truncate) return -ENOSYS;
    return f->inode->ops->truncate(f->inode, length);
}

int vfs_truncate(const char *path, off_t length) {
    struct inode *ino = vfs_resolve(path);
    if (!ino) return -ENOENT;
    if (!ino->ops || !ino->ops->truncate) return -ENOSYS;
    return ino->ops->truncate(ino, length);
}

int vfs_chmod(const char *path, mode_t mode) {
    struct inode *ino = vfs_resolve(path);
    if (!ino) return -ENOENT;
    if (!ino->ops || !ino->ops->chmod) return -ENOSYS;
    return ino->ops->chmod(ino, mode);
}

int vfs_fchmod(int fd, mode_t mode) {
    if (fd < 0 || fd >= VFS_FD_MAX) return -EBADF;
    struct file_desc *f = &fd_table[fd];
    if (!f->inode) return -EBADF;
    if (!f->inode->ops || !f->inode->ops->chmod) return -ENOSYS;
    return f->inode->ops->chmod(f->inode, mode);
}

int vfs_chown(const char *path, uid_t uid, gid_t gid) {
    struct inode *ino = vfs_resolve(path);
    if (!ino) return -ENOENT;
    if (!ino->ops || !ino->ops->chown) return -ENOSYS;
    return ino->ops->chown(ino, uid, gid);
}

int vfs_fchown(int fd, uid_t uid, gid_t gid) {
    if (fd < 0 || fd >= VFS_FD_MAX) return -EBADF;
    struct file_desc *f = &fd_table[fd];
    if (!f->inode) return -EBADF;
    if (!f->inode->ops || !f->inode->ops->chown) return -ENOSYS;
    return f->inode->ops->chown(f->inode, uid, gid);
}

/*
 * vfs_access — check whether the current process can access @path with mode
 * @amode (F_OK=0, R_OK=4, W_OK=2, X_OK=1).  Simplified: always grants access
 * for root (euid==0); for others checks permission bits against st_mode.
 */
int vfs_access(const char *path, int amode) {
    struct inode *ino = vfs_resolve(path);
    if (!ino) return -ENOENT;
    if (amode == 0 /* F_OK */) return 0;   /* file exists */

    struct stat st;
    if (!ino->ops || !ino->ops->stat) return -ENOSYS;
    int r = ino->ops->stat(ino, &st);
    if (r < 0) return r;

    extern struct proc *current;
    if (current && current->euid == 0) return 0;   /* root always succeeds */

    mode_t m = st.st_mode & 07777;
    /* Check other-bits (simplified: we only model other for non-root) */
    int ok = 1;
    if ((amode & 4) && !(m & 0004)) ok = 0;
    if ((amode & 2) && !(m & 0002)) ok = 0;
    if ((amode & 1) && !(m & 0001)) ok = 0;
    return ok ? 0 : -EACCES;
}

int vfs_link(const char *oldpath, const char *newpath) {
    struct inode *ino = vfs_resolve(oldpath);
    if (!ino) return -ENOENT;

    /* Resolve newpath's parent directory and leaf name */
    char parent_path[256];
    const char *leaf = newpath;
    /* Find last '/' */
    int last_slash = -1;
    for (int i = 0; newpath[i]; i++) {
        if (newpath[i] == '/') last_slash = i;
    }
    struct inode *parent;
    if (last_slash <= 0) {
        extern struct inode *vfs_root_inode(void);
        parent = vfs_root_inode();
        leaf   = newpath + (last_slash + 1);
    } else {
        extern size_t strlen(const char *);
        size_t plen = (size_t)last_slash;
        if (plen >= sizeof(parent_path) - 1) return -ENAMETOOLONG;
        extern char *strncpy(char *, const char *, size_t);
        strncpy(parent_path, newpath, plen);
        parent_path[plen] = '\0';
        parent = vfs_resolve(parent_path);
        leaf   = newpath + last_slash + 1;
    }
    if (!parent) return -ENOENT;
    if (!ino->ops || !ino->ops->link) return -ENOSYS;
    return ino->ops->link(ino, parent, leaf);
}

int vfs_symlink(const char *target, const char *linkpath) {
    /* Resolve parent of linkpath */
    char parent_path[256];
    const char *leaf = linkpath;
    int last_slash = -1;
    for (int i = 0; linkpath[i]; i++) {
        if (linkpath[i] == '/') last_slash = i;
    }
    struct inode *parent;
    if (last_slash <= 0) {
        extern struct inode *vfs_root_inode(void);
        parent = vfs_root_inode();
        leaf   = linkpath + (last_slash + 1);
    } else {
        size_t plen = (size_t)last_slash;
        if (plen >= sizeof(parent_path) - 1) return -ENAMETOOLONG;
        extern char *strncpy(char *, const char *, size_t);
        strncpy(parent_path, linkpath, plen);
        parent_path[plen] = '\0';
        parent = vfs_resolve(parent_path);
        leaf   = linkpath + last_slash + 1;
    }
    if (!parent) return -ENOENT;
    if (!parent->ops || !parent->ops->symlink) return -ENOSYS;
    struct inode *sym = parent->ops->symlink(parent, leaf, target);
    return sym ? 0 : -EIO;
}

int vfs_readlink(const char *path, char *buf, size_t size) {
    struct inode *ino = vfs_resolve(path);
    if (!ino) return -ENOENT;
    if (!ino->ops || !ino->ops->readlink) return -EINVAL;
    return ino->ops->readlink(ino, buf, size);
}

