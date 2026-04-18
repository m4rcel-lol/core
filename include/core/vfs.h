/* CORE Kernel — (c) CORE Project, MIT License */
#ifndef CORE_VFS_H
#define CORE_VFS_H

#include <core/types.h>

#define VFS_NAME_MAX   255
#define VFS_MOUNT_MAX  8
#define VFS_INODE_MAX  1024
#define VFS_FD_MAX     256

#define O_RDONLY  0x0000
#define O_WRONLY  0x0001
#define O_RDWR    0x0002
#define O_CREAT   0x0040
#define O_TRUNC   0x0200
#define O_APPEND  0x0400
#define O_NONBLOCK 0x0800

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define S_IFMT   0xF000
#define S_IFREG  0x8000
#define S_IFDIR  0x4000
#define S_IFIFO  0x1000
#define S_IFSOCK 0xC000
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)

#define ENOENT   2
#define EEXIST   17
#define ENOTDIR  20
#define EISDIR   21
#define EINVAL   22
#define ENFILE   23
#define EMFILE   24
#define EFBIG    27
#define ENOSPC   28
#define EROFS    30
#define ENOTEMPTY 39
#define ENOMEM   12
#define EBADF    9
#define EPERM    1
#define EACCES   13
#define EPIPE    32
#define EAGAIN   11
#define EINTR    4
#define ESRCH    3
#define ECHILD   10
#define ERANGE   34
#define ENOSYS   38
#define ENOEXEC  8
#define ENOTCONN    107
#define ECONNREFUSED 111

struct stat {
    dev_t     st_dev;
    ino_t     st_ino;
    mode_t    st_mode;
    nlink_t   st_nlink;
    uid_t     st_uid;
    gid_t     st_gid;
    dev_t     st_rdev;
    off_t     st_size;
    blksize_t st_blksize;
    blkcnt_t  st_blocks;
    uint64_t  st_atime;
    uint64_t  st_mtime;
    uint64_t  st_ctime;
};

struct dirent {
    ino_t  d_ino;
    off_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char   d_name[VFS_NAME_MAX + 1];
};

#define DT_REG  8
#define DT_DIR  4

struct inode;

struct vfs_ops {
    int     (*open)   (struct inode *ino, int flags);
    int     (*close)  (struct inode *ino);
    ssize_t (*read)   (struct inode *ino, void *buf, size_t n, off_t off);
    ssize_t (*write)  (struct inode *ino, const void *buf, size_t n, off_t off);
    int     (*readdir)(struct inode *ino, struct dirent *buf, size_t count, off_t off);
    int     (*stat)   (struct inode *ino, struct stat *st);
    int     (*mkdir)  (struct inode *parent, const char *name, int mode);
    int     (*unlink) (struct inode *parent, const char *name);
    int     (*rename) (struct inode *old_dir, const char *old_name,
                       struct inode *new_dir, const char *new_name);
    struct inode *(*lookup)(struct inode *parent, const char *name);
    struct inode *(*create)(struct inode *parent, const char *name, int mode);
};

struct inode {
    ino_t          ino;
    mode_t         mode;
    off_t          size;
    uint32_t       refcount;
    struct vfs_ops *ops;
    void          *private;
};

struct mount_entry {
    char         path[VFS_NAME_MAX + 1];
    struct inode *root;
    char         fstype[32];
};

struct file_desc {
    struct inode *inode;
    off_t         offset;
    int           flags;
    int           refcount;
    int           is_pipe;
    void         *pipe_ptr;
};

void  vfs_init(void);
int   vfs_register(const char *name, struct vfs_ops *ops,
                   struct inode *(*mount_fn)(void *data), void *data);
int   vfs_mount(const char *path, const char *fstype, void *data);
struct inode *vfs_resolve(const char *path);
struct inode *vfs_resolve_parent(const char *path, char *name_out);
int   vfs_open(const char *path, int flags, int mode);
int   vfs_close(int fd);
ssize_t vfs_read(int fd, void *buf, size_t n);
ssize_t vfs_write(int fd, const void *buf, size_t n);
off_t   vfs_lseek(int fd, off_t off, int whence);
int   vfs_stat(const char *path, struct stat *st);
int   vfs_fstat(int fd, struct stat *st);
int   vfs_mkdir(const char *path, int mode);
int   vfs_unlink(const char *path);
int   vfs_rename(const char *old, const char *new_path);
int   vfs_readdir(int fd, struct dirent *buf, size_t count);
int   vfs_dup(int fd);
int   vfs_dup2(int old, int new_fd);
int   vfs_alloc_fd(struct inode *ino, int flags);
int   vfs_alloc_fd_pipe(void *pipe_ptr, int flags);
struct file_desc *vfs_get_fd(int fd);

#endif /* CORE_VFS_H */
