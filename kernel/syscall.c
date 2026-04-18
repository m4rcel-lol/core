/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>
#include <core/proc.h>
#include <core/syscall.h>
#include <core/vfs.h>
#include <core/ipc.h>
#include <core/signal.h>
#include <core/mm.h>
#include <core/drivers.h>
#include <core/tty.h>

extern struct proc *current;
extern int sys_fork(void);
extern void sys_exit(int code);
extern int sys_wait(int *status);
extern int proc_execve(const char *path, char *argv[], char *envp[]);
extern void *sys_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
extern int sys_munmap(void *addr, size_t len);
extern void *sys_brk(void *addr);
extern uint64_t kernel_uptime_ms(void);
extern void timer_sleep_ms(uint32_t ms);
extern int sys_pipe(int fds[2]);
extern int sys_sigaction(int sig, const struct sigaction *act, struct sigaction *old);
extern int sys_sigreturn(void);
extern int sys_kill(pid_t pid, int sig);

/* __core_version is defined in boot.S */
extern const char __core_version[];

/* Forward declarations for AF_UNIX helpers defined later in this file */
#define SOCK_FD_BASE  100
#define SOCK_MAX      16
static inline int sock_idx(int fd);
static uint64_t unix_sock_send(int idx, const uint8_t *buf, size_t n);
static uint64_t unix_sock_recv(int idx, uint8_t *buf, size_t n);
static void     unix_sock_close(int idx);

static uint64_t do_sys_exit(struct regs *r) {
    sys_exit((int)r->rdi);
    return 0;
}

static uint64_t do_sys_fork(struct regs *r) {
    (void)r;
    return (uint64_t)(int64_t)sys_fork();
}

static uint64_t do_sys_exec(struct regs *r) {
    const char *path = (const char *)r->rdi;
    char **argv = (char **)r->rsi;
    char **envp = (char **)r->rdx;
    return (uint64_t)(int64_t)proc_execve(path, argv, envp);
}

static uint64_t do_sys_wait(struct regs *r) {
    int *status = (int *)r->rdi;
    return (uint64_t)(int64_t)sys_wait(status);
}

static uint64_t do_sys_getpid(struct regs *r) {
    (void)r;
    return current ? current->pid : 0;
}

static uint64_t do_sys_getppid(struct regs *r) {
    (void)r;
    return current ? current->ppid : 0;
}

static uint64_t do_sys_kill(struct regs *r) {
    return (uint64_t)(int64_t)sys_kill((pid_t)r->rdi, (int)r->rsi);
}

static uint64_t do_sys_sigaction(struct regs *r) {
    return (uint64_t)(int64_t)sys_sigaction((int)r->rdi,
        (const struct sigaction *)r->rsi, (struct sigaction *)r->rdx);
}

static uint64_t do_sys_sigreturn(struct regs *r) {
    (void)r;
    return (uint64_t)(int64_t)sys_sigreturn();
}

static uint64_t do_sys_sleep(struct regs *r) {
    timer_sleep_ms((uint32_t)r->rdi);
    return 0;
}

static uint64_t do_sys_open(struct regs *r) {
    return (uint64_t)(int64_t)vfs_open((const char *)r->rdi, (int)r->rsi, (int)r->rdx);
}

static uint64_t do_sys_close(struct regs *r) {
    int fd = (int)r->rdi;
    int sidx = sock_idx(fd);
    if (sidx >= 0) {
        unix_sock_close(sidx);
        return 0;
    }
    return (uint64_t)(int64_t)vfs_close(fd);
}

static uint64_t do_sys_read(struct regs *r) {
    int fd = (int)r->rdi;
    int sidx = sock_idx(fd);
    if (sidx >= 0)
        return unix_sock_recv(sidx, (uint8_t *)r->rsi, r->rdx);
    struct file_desc *fdp = vfs_get_fd(fd);
    if (fdp && fdp->is_pipe) {
        return (uint64_t)(int64_t)pipe_read((struct pipe *)fdp->pipe_ptr,
                                             (void *)r->rsi, r->rdx);
    }
    if (fd == 0) {
        /* stdin: keyboard */
        return (uint64_t)(int64_t)keyboard_buf_read((void *)r->rsi, r->rdx);
    }
    return (uint64_t)(int64_t)vfs_read(fd, (void *)r->rsi, r->rdx);
}

static uint64_t do_sys_write(struct regs *r) {
    int fd = (int)r->rdi;
    int sidx = sock_idx(fd);
    if (sidx >= 0)
        return unix_sock_send(sidx, (const uint8_t *)r->rsi, r->rdx);
    struct file_desc *fdp = vfs_get_fd(fd);
    if (fdp && fdp->is_pipe) {
        return (uint64_t)(int64_t)pipe_write((struct pipe *)fdp->pipe_ptr,
                                              (const void *)r->rsi, r->rdx);
    }
    if (fd == 1 || fd == 2) {
        const char *buf = (const char *)r->rsi;
        size_t n = r->rdx;
        for (size_t i = 0; i < n; i++) kprintf("%c", buf[i]);
        return (uint64_t)n;
    }
    return (uint64_t)(int64_t)vfs_write(fd, (const void *)r->rsi, r->rdx);
}

static uint64_t do_sys_lseek(struct regs *r) {
    return (uint64_t)(int64_t)vfs_lseek((int)r->rdi, (off_t)r->rsi, (int)r->rdx);
}

static uint64_t do_sys_fstat(struct regs *r) {
    return (uint64_t)(int64_t)vfs_fstat((int)r->rdi, (struct stat *)r->rsi);
}

static uint64_t do_sys_stat(struct regs *r) {
    return (uint64_t)(int64_t)vfs_stat((const char *)r->rdi, (struct stat *)r->rsi);
}

static uint64_t do_sys_dup(struct regs *r) {
    return (uint64_t)(int64_t)vfs_dup((int)r->rdi);
}

static uint64_t do_sys_dup2(struct regs *r) {
    return (uint64_t)(int64_t)vfs_dup2((int)r->rdi, (int)r->rsi);
}

static uint64_t do_sys_pipe(struct regs *r) {
    return (uint64_t)(int64_t)sys_pipe((int *)r->rdi);
}

static uint64_t do_sys_mkdir(struct regs *r) {
    return (uint64_t)(int64_t)vfs_mkdir((const char *)r->rdi, (int)r->rsi);
}

static uint64_t do_sys_rmdir(struct regs *r) {
    return (uint64_t)(int64_t)vfs_unlink((const char *)r->rdi);
}

static uint64_t do_sys_unlink(struct regs *r) {
    return (uint64_t)(int64_t)vfs_unlink((const char *)r->rdi);
}

static uint64_t do_sys_rename(struct regs *r) {
    return (uint64_t)(int64_t)vfs_rename((const char *)r->rdi, (const char *)r->rsi);
}

static uint64_t do_sys_chdir(struct regs *r) {
    const char *path = (const char *)r->rdi;
    struct inode *ino = vfs_resolve(path);
    if (!ino) return (uint64_t)(int64_t)-ENOENT;
    if (!S_ISDIR(ino->mode)) return (uint64_t)(int64_t)-ENOTDIR;
    if (!current) return (uint64_t)(int64_t)-EINVAL;
    extern size_t strlen(const char *);
    extern char *strncpy(char *, const char *, size_t);
    strncpy(current->cwd, path, 255);
    current->cwd[255] = '\0';
    return 0;
}

static uint64_t do_sys_getcwd(struct regs *r) {
    char *buf = (char *)r->rdi;
    size_t len = r->rsi;
    if (!current || !buf || !len) return (uint64_t)(int64_t)-EINVAL;
    extern size_t strlen(const char *);
    size_t cwdlen = strlen(current->cwd);
    if (len < cwdlen + 1) return (uint64_t)(int64_t)-ERANGE;
    extern char *strncpy(char *, const char *, size_t);
    strncpy(buf, current->cwd, len);
    return 0;
}

static uint64_t do_sys_readdir(struct regs *r) {
    return (uint64_t)(int64_t)vfs_readdir((int)r->rdi,
        (struct dirent *)r->rsi, r->rdx);
}

static uint64_t do_sys_mmap(struct regs *r) {
    return (uint64_t)sys_mmap((void *)r->rdi, r->rsi, (int)r->rdx,
                               (int)r->r10, (int)r->r8, (off_t)r->r9);
}

static uint64_t do_sys_munmap(struct regs *r) {
    return (uint64_t)(int64_t)sys_munmap((void *)r->rdi, r->rsi);
}

static uint64_t do_sys_brk(struct regs *r) {
    return (uint64_t)sys_brk((void *)r->rdi);
}

/* AF_UNIX socket implementation
 *
 * Each socket gets a file descriptor in the range [SOCK_FD_BASE,
 * SOCK_FD_BASE + SOCK_MAX).  Sockets communicate via per-socket
 * circular receive buffers: send() writes to the peer's rbuf,
 * recv() reads from the caller's own rbuf.
 *
 * bind(fd, sockaddr_un, len)   — registers a path on the socket
 * connect(fd, sockaddr_un, len)— links to the socket bound at that path
 * send(fd, buf, n, flags)      — writes n bytes into the peer's buffer
 * recv(fd, buf, n, flags)      — reads n bytes from the socket's buffer
 * close(fd)                    — signals EOF to peer and frees the slot
 */
#define UNIX_BUF_SIZE 4096

struct unix_sock {
    int     used;
    int     domain;                  /* AF_UNIX */
    int     type;                    /* SOCK_STREAM or SOCK_DGRAM */
    char    path[UNIX_PATH_MAX];     /* bound address; empty if unbound */
    int     peer;                    /* socks[] index of connected peer, or -1 */
    /* circular receive buffer */
    uint8_t rbuf[UNIX_BUF_SIZE];
    size_t  rbuf_head;               /* index of next byte to read */
    size_t  rbuf_len;                /* bytes currently buffered */
    int     eof;                     /* peer closed — no more incoming data */
};

static struct unix_sock socks[SOCK_MAX];

/* Return the socks[] index for socket fd, or -1 if invalid */
static inline int sock_idx(int fd) {
    int i = fd - SOCK_FD_BASE;
    return (i >= 0 && i < SOCK_MAX && socks[i].used) ? i : -1;
}

static uint64_t do_sys_socket(struct regs *r) {
    int domain = (int)r->rdi, type = (int)r->rsi;
    if (domain != AF_UNIX) return (uint64_t)(int64_t)-EINVAL;
    if (type != SOCK_STREAM && type != SOCK_DGRAM) return (uint64_t)(int64_t)-EINVAL;
    for (int i = 0; i < SOCK_MAX; i++) {
        if (!socks[i].used) {
            socks[i].used      = 1;
            socks[i].domain    = domain;
            socks[i].type      = type;
            socks[i].path[0]   = '\0';
            socks[i].peer      = -1;
            socks[i].rbuf_head = 0;
            socks[i].rbuf_len  = 0;
            socks[i].eof       = 0;
            return (uint64_t)(SOCK_FD_BASE + i);
        }
    }
    return (uint64_t)(int64_t)-EMFILE;
}

static uint64_t do_sys_bind(struct regs *r) {
    int idx = sock_idx((int)r->rdi);
    if (idx < 0) return (uint64_t)(int64_t)-EBADF;
    const struct sockaddr_un *sa = (const struct sockaddr_un *)r->rsi;
    if (!sa || sa->sun_family != AF_UNIX) return (uint64_t)(int64_t)-EINVAL;
    extern char *strncpy(char *, const char *, size_t);
    strncpy(socks[idx].path, sa->sun_path, UNIX_PATH_MAX - 1);
    socks[idx].path[UNIX_PATH_MAX - 1] = '\0';
    return 0;
}

static uint64_t do_sys_connect(struct regs *r) {
    int idx = sock_idx((int)r->rdi);
    if (idx < 0) return (uint64_t)(int64_t)-EBADF;
    const struct sockaddr_un *sa = (const struct sockaddr_un *)r->rsi;
    if (!sa || sa->sun_family != AF_UNIX) return (uint64_t)(int64_t)-EINVAL;
    extern int strcmp(const char *, const char *);
    for (int i = 0; i < SOCK_MAX; i++) {
        if (i != idx && socks[i].used && socks[i].path[0] != '\0' &&
            strcmp(socks[i].path, sa->sun_path) == 0) {
            socks[idx].peer = i;
            socks[i].peer   = idx;
            return 0;
        }
    }
    return (uint64_t)(int64_t)-ECONNREFUSED;
}

/* Write n bytes from buf into the peer's circular receive buffer */
static uint64_t unix_sock_send(int idx, const uint8_t *buf, size_t n) {
    int peer = socks[idx].peer;
    if (peer < 0) return (uint64_t)(int64_t)-ENOTCONN;
    struct unix_sock *p = &socks[peer];
    size_t avail = UNIX_BUF_SIZE - p->rbuf_len;
    if (n > avail) n = avail;
    for (size_t i = 0; i < n; i++) {
        size_t pos = (p->rbuf_head + p->rbuf_len) % UNIX_BUF_SIZE;
        p->rbuf[pos] = buf[i];
        p->rbuf_len++;
    }
    return (uint64_t)n;
}

/* Read up to n bytes from the socket's own receive buffer */
static uint64_t unix_sock_recv(int idx, uint8_t *buf, size_t n) {
    struct unix_sock *s = &socks[idx];
    if (s->rbuf_len == 0 && s->eof) return 0;          /* EOF */
    if (s->rbuf_len == 0)           return (uint64_t)(int64_t)-EAGAIN;
    if (n > s->rbuf_len) n = s->rbuf_len;
    for (size_t i = 0; i < n; i++) {
        buf[i] = s->rbuf[s->rbuf_head];
        s->rbuf_head = (s->rbuf_head + 1) % UNIX_BUF_SIZE;
        s->rbuf_len--;
    }
    return (uint64_t)n;
}

static uint64_t do_sys_send(struct regs *r) {
    int idx = sock_idx((int)r->rdi);
    if (idx < 0) return (uint64_t)(int64_t)-EBADF;
    return unix_sock_send(idx, (const uint8_t *)r->rsi, r->rdx);
}

static uint64_t do_sys_recv(struct regs *r) {
    int idx = sock_idx((int)r->rdi);
    if (idx < 0) return (uint64_t)(int64_t)-EBADF;
    return unix_sock_recv(idx, (uint8_t *)r->rsi, r->rdx);
}

/* Release a socket slot, signalling EOF to the connected peer */
static void unix_sock_close(int idx) {
    int peer = socks[idx].peer;
    if (peer >= 0 && socks[peer].used) {
        socks[peer].eof  = 1;   /* peer will see 0-byte read after draining */
        socks[peer].peer = -1;
    }
    socks[idx].used      = 0;
    socks[idx].peer      = -1;
    socks[idx].path[0]   = '\0';
    socks[idx].rbuf_len  = 0;
    socks[idx].rbuf_head = 0;
    socks[idx].eof       = 0;
}

static uint64_t do_sys_uname(struct regs *r) {
    struct utsname *buf = (struct utsname *)r->rdi;
    if (!buf) return (uint64_t)(int64_t)-EINVAL;
    extern char *strncpy(char *, const char *, size_t);
    strncpy(buf->sysname,  "CORE",       64);
    strncpy(buf->nodename, "core",       64);
    strncpy(buf->release,  "0.1.0",      64);
    strncpy(buf->version,  "CORE-0.1.0", 64);
    strncpy(buf->machine,  "x86_64",     64);
    return 0;
}

static uint64_t do_sys_gettimeofday(struct regs *r) {
    struct timeval *tv = (struct timeval *)r->rdi;
    if (!tv) return (uint64_t)(int64_t)-EINVAL;
    uint64_t ms = kernel_uptime_ms();
    tv->tv_sec  = (int64_t)(ms / 1000);
    tv->tv_usec = (int64_t)((ms % 1000) * 1000);
    return 0;
}

/*
 * sys_fcntl — file-descriptor control
 *
 * Supported commands:
 *   F_GETFL  — return file status flags (mode bits + O_NONBLOCK/O_APPEND)
 *   F_SETFL  — update O_NONBLOCK and O_APPEND in flags
 *   F_GETFD  — return FD_CLOEXEC if O_CLOEXEC is set on the descriptor
 *   F_SETFD  — set or clear O_CLOEXEC on the descriptor
 *   F_DUPFD  — duplicate fd to the lowest available fd >= arg
 */
int sys_fcntl(int fd, int cmd, int arg) {
    struct file_desc *fdp = vfs_get_fd(fd);

    switch (cmd) {
    case F_GETFL:
        if (!fdp) return -EBADF;
        /* Return status flags; mask out the descriptor-level CLOEXEC bit */
        return fdp->flags & ~O_CLOEXEC;

    case F_SETFL:
        if (!fdp) return -EBADF;
        fdp->flags = (fdp->flags & ~SETFL_MASK) | (arg & SETFL_MASK);
        return 0;

    case F_GETFD:
        if (!fdp) return -EBADF;
        return (fdp->flags & O_CLOEXEC) ? FD_CLOEXEC : 0;

    case F_SETFD:
        if (!fdp) return -EBADF;
        if (arg & FD_CLOEXEC)
            fdp->flags |=  O_CLOEXEC;
        else
            fdp->flags &= ~O_CLOEXEC;
        return 0;

    case F_DUPFD:
        return vfs_dupfd(fd, arg);

    default:
        return -EINVAL;
    }
}

static uint64_t do_sys_fcntl(struct regs *r) {
    return (uint64_t)(int64_t)sys_fcntl((int)r->rdi, (int)r->rsi, (int)r->rdx);
}

/*
 * sys_ioctl — I/O device control
 *
 * Supported requests:
 *   TCGETS       — fill *argp with sane cooked-mode termios defaults
 *   TCSETS/W/F   — accept a new termios but ignore it (console is fixed)
 *   TCFLSH       — flush queues (no-op)
 *   TIOCGWINSZ   — return 80×25 terminal size
 *   TIOCSWINSZ   — accept new window size but ignore it
 *   FIONREAD     — return bytes available: pipe buffer count, socket rbuf_len,
 *                  or 0 for other fds / tty
 *   TIOCGPGRP    — return foreground process group (current PID or 1)
 *   TIOCSPGRP    — set foreground process group (ignored)
 */
int sys_ioctl(int fd, unsigned long req, void *argp) {
    extern void *memset(void *, int, size_t);

    switch (req) {
    /* ── Terminal attribute get ─────────────────────────────────────────── */
    case TCGETS: {
        struct termios *t = (struct termios *)argp;
        if (!t) return -EINVAL;
        memset(t, 0, sizeof(*t));
        t->c_iflag = CORE_TERM_IFLAG;
        t->c_oflag = CORE_TERM_OFLAG;
        t->c_cflag = CORE_TERM_CFLAG;
        t->c_lflag = CORE_TERM_LFLAG;
        t->c_cc[VINTR]  = 0x03;  /* ^C */
        t->c_cc[VQUIT]  = 0x1C;  /* ^\ */
        t->c_cc[VERASE] = 0x7F;  /* DEL */
        t->c_cc[VKILL]  = 0x15;  /* ^U */
        t->c_cc[VEOF]   = 0x04;  /* ^D */
        t->c_cc[VSUSP]  = 0x1A;  /* ^Z */
        t->c_cc[VSTART] = 0x11;  /* ^Q */
        t->c_cc[VSTOP]  = 0x13;  /* ^S */
        t->c_cc[VMIN]   = 1;
        t->c_cc[VTIME]  = 0;
        return 0;
    }

    /* ── Terminal attribute set (cooked console — ignore) ───────────────── */
    case TCSETS:
    case TCSETSW:
    case TCSETSF:
        return 0;

    /* ── Flush queues (no-op for our simple console) ────────────────────── */
    case TCFLSH:
        return 0;

    /* ── Window size get ────────────────────────────────────────────────── */
    case TIOCGWINSZ: {
        struct winsize *ws = (struct winsize *)argp;
        if (!ws) return -EINVAL;
        ws->ws_row    = 25;
        ws->ws_col    = 80;
        ws->ws_xpixel = 0;
        ws->ws_ypixel = 0;
        return 0;
    }

    /* ── Window size set (ignore) ───────────────────────────────────────── */
    case TIOCSWINSZ:
        return 0;

    /* ── Bytes available to read ────────────────────────────────────────── */
    case FIONREAD: {
        int *count = (int *)argp;
        if (!count) return -EINVAL;
        /* Socket fd */
        int sidx = sock_idx(fd);
        if (sidx >= 0) {
            *count = (int)socks[sidx].rbuf_len;
            return 0;
        }
        /* Pipe fd */
        struct file_desc *fdp = vfs_get_fd(fd);
        if (fdp && fdp->is_pipe && fdp->pipe_ptr) {
            *count = (int)((struct pipe *)fdp->pipe_ptr)->count;
            return 0;
        }
        /* Stdin / stdout / stderr and regular files: 0 */
        *count = 0;
        return 0;
    }

    /* ── Foreground process group get ───────────────────────────────────── */
    case TIOCGPGRP: {
        pid_t *pgid = (pid_t *)argp;
        if (!pgid) return -EINVAL;
        *pgid = current ? (pid_t)current->pid : 1;
        return 0;
    }

    /* ── Foreground process group set (ignore) ──────────────────────────── */
    case TIOCSPGRP:
        return 0;

    default:
        return -EINVAL;
    }
}

static uint64_t do_sys_ioctl(struct regs *r) {
    return (uint64_t)(int64_t)sys_ioctl((int)r->rdi, r->rsi, (void *)r->rdx);
}

static uint64_t do_sys_reboot(struct regs *r) {
    if ((uint32_t)r->rdi != REBOOT_MAGIC) return (uint64_t)(int64_t)-EINVAL;
    kprintf("CORE: rebooting...\n");
    /* Triple fault by loading null IDT */
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) null_idt = {0, 0};
    __asm__ volatile("lidt %0; int $3" : : "m"(null_idt));
    while(1) {}
    return 0;
}

/*
 * sys_select — synchronous I/O multiplexing.
 *
 * For each fd in [0, nfds) the function tests:
 *   read-ready:  regular file always ready; pipe read-end ready if count > 0;
 *                AF_UNIX socket ready if rbuf_len > 0.
 *   write-ready: regular file and pipe write-end always ready (no back-pressure
 *                modelled beyond PIPE_BUF_SIZE, which is checked elsewhere).
 *
 * If no fd is immediately ready and timeout != NULL and timeout has a non-zero
 * duration, the caller sleeps for that duration (or at least 10 ms, one tick)
 * before performing one final check.  A zero timeout (poll) returns immediately.
 *
 * exceptfds is always cleared; the kernel raises no out-of-band conditions.
 *
 * Returns the total number of ready descriptors, or -errno on error.
 */
static int fd_is_read_ready(int fd) {
    struct file_desc *fdp = vfs_get_fd(fd);
    if (!fdp) return 0;
    /* Pipe: ready if data available */
    if (fdp->is_pipe && fdp->pipe_ptr) {
        struct pipe *p = (struct pipe *)fdp->pipe_ptr;
        if (p->count > 0) return 1;
        if (p->write_closed) return 1;   /* EOF is readable */
        return 0;
    }
    /* Socket: ready if buffer has data or peer closed */
    int sidx = sock_idx(fd);
    if (sidx >= 0) {
        return (socks[sidx].rbuf_len > 0 || socks[sidx].eof) ? 1 : 0;
    }
    /* Regular file / directory: always ready */
    return 1;
}

static int fd_is_write_ready(int fd) {
    struct file_desc *fdp = vfs_get_fd(fd);
    if (!fdp) return 0;
    /* Pipe: ready if there is space and read-end is still open */
    if (fdp->is_pipe && fdp->pipe_ptr) {
        struct pipe *p = (struct pipe *)fdp->pipe_ptr;
        if (p->read_closed) return 0;   /* SIGPIPE territory */
        return (p->count < PIPE_BUF_SIZE) ? 1 : 0;
    }
    /* Socket: always consider writable (no send-buffer limit modelled) */
    int sidx = sock_idx(fd);
    if (sidx >= 0) {
        return (socks[sidx].peer >= 0) ? 1 : 0;
    }
    /* Regular file: always ready */
    return 1;
}

int sys_select(int nfds, fd_set *readfds, fd_set *writefds,
               fd_set *exceptfds, struct timeval *timeout) {
    if (nfds < 0 || nfds > FD_SETSIZE) return -EINVAL;

    /* Clear exceptfds — no OOB data in this kernel */
    if (exceptfds) { FD_ZERO(exceptfds); }

    /* Helper: scan all requested fds and fill in the ready ones */
    int ready = 0;

    /* We may need to loop twice (once now, once after sleeping). */
    int attempts = 2;
    uint32_t sleep_ms = 0;

    if (timeout) {
        if (timeout->tv_sec == 0 && timeout->tv_usec == 0) {
            attempts = 1;   /* pure poll: single pass, no sleeping */
        } else {
            int64_t ms = timeout->tv_sec * 1000 + timeout->tv_usec / 1000;
            sleep_ms = (ms > 0) ? (uint32_t)ms : 10U;
        }
    }

    /* Snapshot the caller's fd_sets so we can overwrite in-place */
    fd_set r_in = {0}, w_in = {0};
    if (readfds)  r_in  = *readfds;
    if (writefds) w_in  = *writefds;

    for (int attempt = 0; attempt < attempts; attempt++) {
        if (attempt > 0 && sleep_ms > 0) {
            timer_sleep_ms(sleep_ms);
        }

        ready = 0;
        if (readfds)  { FD_ZERO(readfds);  }
        if (writefds) { FD_ZERO(writefds); }

        for (int fd = 0; fd < nfds; fd++) {
            if (readfds && FD_ISSET(fd, &r_in)) {
                if (fd_is_read_ready(fd)) { FD_SET(fd, readfds); ready++; }
            }
            if (writefds && FD_ISSET(fd, &w_in)) {
                if (fd_is_write_ready(fd)) { FD_SET(fd, writefds); ready++; }
            }
        }

        if (ready > 0 || attempts == 1) break;
    }

    return ready;
}

static uint64_t do_sys_select(struct regs *r) {
    return (uint64_t)(int64_t)sys_select(
        (int)r->rdi,
        (fd_set *)r->rsi,
        (fd_set *)r->rdx,
        (fd_set *)r->r10,
        (struct timeval *)r->r8);
}

static uint64_t do_sys_ftruncate(struct regs *r) {
    return (uint64_t)(int64_t)vfs_ftruncate((int)r->rdi, (off_t)r->rsi);
}

static uint64_t do_sys_truncate(struct regs *r) {
    return (uint64_t)(int64_t)vfs_truncate((const char *)r->rdi, (off_t)r->rsi);
}

typedef uint64_t (*syscall_fn_t)(struct regs *r);

static syscall_fn_t syscall_table[SYSCALL_COUNT] = {
    [SYS_EXIT]        = do_sys_exit,
    [SYS_FORK]        = do_sys_fork,
    [SYS_EXEC]        = do_sys_exec,
    [SYS_WAIT]        = do_sys_wait,
    [SYS_GETPID]      = do_sys_getpid,
    [SYS_GETPPID]     = do_sys_getppid,
    [SYS_KILL]        = do_sys_kill,
    [SYS_SIGACTION]   = do_sys_sigaction,
    [SYS_SIGRETURN]   = do_sys_sigreturn,
    [SYS_SLEEP]       = do_sys_sleep,
    [SYS_OPEN]        = do_sys_open,
    [SYS_CLOSE]       = do_sys_close,
    [SYS_READ]        = do_sys_read,
    [SYS_WRITE]       = do_sys_write,
    [SYS_LSEEK]       = do_sys_lseek,
    [SYS_FSTAT]       = do_sys_fstat,
    [SYS_STAT]        = do_sys_stat,
    [SYS_DUP]         = do_sys_dup,
    [SYS_DUP2]        = do_sys_dup2,
    [SYS_PIPE]        = do_sys_pipe,
    [SYS_MKDIR]       = do_sys_mkdir,
    [SYS_RMDIR]       = do_sys_rmdir,
    [SYS_UNLINK]      = do_sys_unlink,
    [SYS_RENAME]      = do_sys_rename,
    [SYS_CHDIR]       = do_sys_chdir,
    [SYS_GETCWD]      = do_sys_getcwd,
    [SYS_READDIR]     = do_sys_readdir,
    [SYS_MMAP]        = do_sys_mmap,
    [SYS_MUNMAP]      = do_sys_munmap,
    [SYS_BRK]         = do_sys_brk,
    [SYS_SOCKET]      = do_sys_socket,
    [SYS_BIND]        = do_sys_bind,
    [SYS_CONNECT]     = do_sys_connect,
    [SYS_SEND]        = do_sys_send,
    [SYS_RECV]        = do_sys_recv,
    [SYS_UNAME]       = do_sys_uname,
    [SYS_GETTIMEOFDAY]= do_sys_gettimeofday,
    [SYS_IOCTL]       = do_sys_ioctl,
    [SYS_FCNTL]       = do_sys_fcntl,
    [SYS_REBOOT]      = do_sys_reboot,
    [SYS_SELECT]      = do_sys_select,
    [SYS_FTRUNCATE]   = do_sys_ftruncate,
    [SYS_TRUNCATE]    = do_sys_truncate,
};

/* MSR addresses for SYSCALL/SYSRET */
#define MSR_EFER  0xC0000080
#define MSR_STAR  0xC0000081
#define MSR_LSTAR 0xC0000082
#define MSR_SFMASK 0xC0000084

static void wrmsr(uint32_t msr, uint64_t val) {
    __asm__ volatile("wrmsr" : : "c"(msr),
                     "a"((uint32_t)(val & 0xFFFFFFFF)),
                     "d"((uint32_t)(val >> 32)));
}

extern void syscall_entry(void);

/* Per-CPU SYSCALL data (simplified: single CPU) */
struct cpu_data {
    uint64_t kernel_rsp;
    uint64_t user_rsp;
} __attribute__((packed));

static struct cpu_data cpu_data_0 __attribute__((aligned(16)));
static uint8_t syscall_kstack[4096] __attribute__((aligned(16)));

void syscall_init(void) {
    cpu_data_0.kernel_rsp = (uint64_t)&syscall_kstack[sizeof(syscall_kstack)];
    cpu_data_0.user_rsp   = 0;

    /* EFER.SCE = 1 (enable SYSCALL) */
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(MSR_EFER));
    lo |= 1;
    __asm__ volatile("wrmsr" : : "c"(MSR_EFER), "a"(lo), "d"(hi));

    /* STAR: selector bases
     * GDT layout: 0x08=kernel code, 0x10=kernel data, 0x18=user data, 0x20=user code
     * bits 47:32 = SYSCALL CS (kernel code = 0x08)
     * bits 63:48 = SYSRET base: CPU loads CS=base+16, SS=base+8
     *   → base=0x10 gives CS=0x20 (user code) and SS=0x18 (user data) ✓
     */
    wrmsr(MSR_STAR, ((uint64_t)0x0010 << 48) | ((uint64_t)0x0008 << 32));

    /* LSTAR: 64-bit SYSCALL handler */
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);

    /* SFMASK: clear IF, DF, TF on SYSCALL */
    wrmsr(MSR_SFMASK, (1 << 9) | (1 << 10) | (1 << 8));

    /* Load GS base for per-CPU data */
    wrmsr(0xC0000101, (uint64_t)&cpu_data_0); /* GS.Base */
    wrmsr(0xC0000102, (uint64_t)&cpu_data_0); /* KernelGSBase */
}

uint64_t syscall_dispatch(struct regs *r) {
    uint64_t num = r->rax;
    if (num >= SYSCALL_COUNT || !syscall_table[num]) {
        return (uint64_t)(int64_t)-ENOSYS;
    }
    return syscall_table[num](r);
}

/* Kernel-callable wrappers for AF_UNIX sockets (used by BIST) */
int sys_socket_unix(int type) {
    for (int i = 0; i < SOCK_MAX; i++) {
        if (!socks[i].used) {
            socks[i].used      = 1;
            socks[i].domain    = AF_UNIX;
            socks[i].type      = type;
            socks[i].path[0]   = '\0';
            socks[i].peer      = -1;
            socks[i].rbuf_head = 0;
            socks[i].rbuf_len  = 0;
            socks[i].eof       = 0;
            return SOCK_FD_BASE + i;
        }
    }
    return -EMFILE;
}

int sys_bind_unix(int fd, const char *path) {
    int idx = sock_idx(fd);
    if (idx < 0) return -EBADF;
    extern char *strncpy(char *, const char *, size_t);
    strncpy(socks[idx].path, path, UNIX_PATH_MAX - 1);
    socks[idx].path[UNIX_PATH_MAX - 1] = '\0';
    return 0;
}

int sys_connect_unix(int fd, const char *path) {
    int idx = sock_idx(fd);
    if (idx < 0) return -EBADF;
    extern int strcmp(const char *, const char *);
    for (int i = 0; i < SOCK_MAX; i++) {
        if (i != idx && socks[i].used && socks[i].path[0] != '\0' &&
            strcmp(socks[i].path, path) == 0) {
            socks[idx].peer = i;
            socks[i].peer   = idx;
            return 0;
        }
    }
    return -ECONNREFUSED;
}

ssize_t sys_send_unix(int fd, const void *buf, size_t n) {
    int idx = sock_idx(fd);
    if (idx < 0) return -EBADF;
    return (ssize_t)unix_sock_send(idx, (const uint8_t *)buf, n);
}

ssize_t sys_recv_unix(int fd, void *buf, size_t n) {
    int idx = sock_idx(fd);
    if (idx < 0) return -EBADF;
    return (ssize_t)unix_sock_recv(idx, (uint8_t *)buf, n);
}

void sys_close_unix(int fd) {
    int idx = sock_idx(fd);
    if (idx >= 0) unix_sock_close(idx);
}
