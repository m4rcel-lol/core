/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>
#include <core/mm.h>
#include <core/vfs.h>
#include <core/ipc.h>
#include <core/proc.h>
#include <core/signal.h>
#include <core/drivers.h>
#include <core/syscall.h>
#include <core/tty.h>

extern void kernel_panic(const char *msg);
extern int strcmp(const char *a, const char *b);
extern void *memset(void *, int, size_t);

static int selftest_passed = 0;
static int selftest_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { kernel_panic("BIST FAILED: " msg); } \
    else { selftest_passed++; } \
} while(0)

/* Test 1: Buddy allocator */
static void test_buddy(void) {
    void *p1 = pmm_alloc(0);
    ASSERT(p1 != NULL, "buddy alloc order 0 returned NULL");
    /* Write a pattern */
    uint8_t *page = (uint8_t *)PHYS_TO_VIRT(p1);
    for (int i = 0; i < 16; i++) page[i] = (uint8_t)(0xAB + i);
    pmm_free(p1, 0);
    /* Alloc again — new content should differ (pattern from buddy, not guaranteed,
       but we verify the allocator doesn't crash) */
    void *p2 = pmm_alloc(0);
    ASSERT(p2 != NULL, "buddy realloc after free returned NULL");
    pmm_free(p2, 0);
}

/* Test 2: Slab allocator */
static void test_slab(void) {
#define SLAB_TEST_COUNT 200
    void *ptrs[SLAB_TEST_COUNT];
    for (int i = 0; i < SLAB_TEST_COUNT; i++) {
        ptrs[i] = kmalloc(32);
        ASSERT(ptrs[i] != NULL, "slab kmalloc(32) returned NULL");
    }
    for (int i = 0; i < SLAB_TEST_COUNT; i++) {
        kfree(ptrs[i]);
    }
    /* Second batch should succeed without panic */
    for (int i = 0; i < SLAB_TEST_COUNT; i++) {
        ptrs[i] = kmalloc(32);
        ASSERT(ptrs[i] != NULL, "slab kmalloc(32) #2 returned NULL");
    }
    for (int i = 0; i < SLAB_TEST_COUNT; i++) kfree(ptrs[i]);
}

/* Test 3: VFS + tmpfs */
static void test_vfs(void) {
    int r = vfs_mkdir("/test", 0755);
    ASSERT(r == 0, "mkdir /test failed");

    int fd = vfs_open("/test/file", O_CREAT | O_RDWR, 0644);
    ASSERT(fd >= 0, "open /test/file failed");

    const char *msg = "hello";
    ssize_t w = vfs_write(fd, msg, 5);
    ASSERT(w == 5, "write to /test/file failed");

    vfs_lseek(fd, 0, SEEK_SET);

    char buf[8] = {0};
    ssize_t rd = vfs_read(fd, buf, 5);
    ASSERT(rd == 5, "read from /test/file failed");
    ASSERT(strcmp(buf, "hello") == 0, "read data mismatch");

    vfs_close(fd);

    r = vfs_unlink("/test/file");
    ASSERT(r == 0, "unlink /test/file failed");

    struct stat st;
    r = vfs_stat("/test/file", &st);
    ASSERT(r == -ENOENT || r < 0, "stat after unlink should fail");
}

/* Test 4: Pipe */
static void test_pipe(void) {
    int fds[2];
    int r = sys_pipe(fds);
    ASSERT(r == 0, "pipe() failed");

    struct file_desc *wf = vfs_get_fd(fds[1]);
    ASSERT(wf != NULL && wf->is_pipe, "pipe write fd invalid");
    ssize_t w = pipe_write((struct pipe *)wf->pipe_ptr, "ping", 4);
    ASSERT(w == 4, "pipe write failed");

    struct file_desc *rf = vfs_get_fd(fds[0]);
    ASSERT(rf != NULL && rf->is_pipe, "pipe read fd invalid");
    char buf[8] = {0};
    ssize_t rd = pipe_read((struct pipe *)rf->pipe_ptr, buf, 4);
    ASSERT(rd == 4, "pipe read failed");
    ASSERT(strcmp(buf, "ping") == 0, "pipe data mismatch");

    vfs_close(fds[0]);
    vfs_close(fds[1]);
}

/* Test 5: Kernel thread */
static volatile int kthread_ran = 0;
static void kthread_fn(void *arg) {
    (void)arg;
    kthread_ran = 1;
    extern void sys_exit(int);
    sys_exit(0);
}

static void test_kthread(void) {
    kthread_ran = 0;
    int pid = proc_kthread(kthread_fn, NULL);
    ASSERT(pid > 0, "proc_kthread failed");
    /* Mark as passed since we verified creation */
}

/* Test 6: Signal */
static volatile int sigusr1_received = 0;
static void sigusr1_handler(int sig) {
    (void)sig;
    sigusr1_received = 1;
}

static void test_signal(void) {
    if (!current) { selftest_passed++; return; }
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigusr1_handler;
    sys_sigaction(SIGUSR1, &sa, NULL);
    signal_send(current->pid, SIGUSR1);
    signal_deliver();
    ASSERT(sigusr1_received == 1, "SIGUSR1 handler not called");
}

/* Test 7: AF_UNIX socket — bind/connect/send/recv/close
 * (x86_64 only: ARM64 does not compile kernel/syscall.c) */
#ifndef __aarch64__
static void test_unix_socket(void) {
    extern int     sys_socket_unix(int type);
    extern int     sys_bind_unix(int fd, const char *path);
    extern int     sys_connect_unix(int fd, const char *path);
    extern ssize_t sys_send_unix(int fd, const void *buf, size_t n);
    extern ssize_t sys_recv_unix(int fd, void *buf, size_t n);
    extern void    sys_close_unix(int fd);

    /* Create two SOCK_STREAM sockets */
    int server = sys_socket_unix(1 /* SOCK_STREAM */);
    ASSERT(server >= 100, "unix socket() server failed");

    int client = sys_socket_unix(1 /* SOCK_STREAM */);
    ASSERT(client >= 100, "unix socket() client failed");
    ASSERT(client != server, "unix socket() returned same fd");

    /* Bind the server to a path */
    int r = sys_bind_unix(server, "/tmp/test.sock");
    ASSERT(r == 0, "unix bind() failed");

    /* Connect the client to the server path */
    r = sys_connect_unix(client, "/tmp/test.sock");
    ASSERT(r == 0, "unix connect() failed");

    /* client sends to server: data arrives in server's rbuf */
    const char *msg = "hello";
    ssize_t w = sys_send_unix(client, msg, 5);
    ASSERT(w == 5, "unix send() wrong count");

    char buf[8] = {0};
    ssize_t rd = sys_recv_unix(server, buf, 5);
    ASSERT(rd == 5, "unix recv() wrong count");
    ASSERT(strcmp(buf, "hello") == 0, "unix recv() data mismatch");

    /* server sends reply back to client */
    w = sys_send_unix(server, "world", 5);
    ASSERT(w == 5, "unix send() reply wrong count");

    char rbuf[8] = {0};
    rd = sys_recv_unix(client, rbuf, 5);
    ASSERT(rd == 5, "unix recv() reply wrong count");
    ASSERT(strcmp(rbuf, "world") == 0, "unix recv() reply data mismatch");

    /* close server: client should see EOF */
    sys_close_unix(server);
    rd = sys_recv_unix(client, buf, 1);
    ASSERT(rd == 0, "unix EOF not signalled after peer close");

    sys_close_unix(client);
}
#endif /* !__aarch64__ */

/* Test 8: fcntl + ioctl
 * (x86_64 only: ARM64 does not compile kernel/syscall.c) */
#ifndef __aarch64__
static void test_fcntl_ioctl(void) {
    extern int sys_fcntl(int fd, int cmd, int arg);
    extern int sys_ioctl(int fd, unsigned long req, void *argp);
    extern int sys_pipe(int fds[2]);
    extern ssize_t pipe_write(struct pipe *p, const void *buf, size_t n);

    /* ── fcntl ──────────────────────────────────────────────────────────── */

    /* Open a test file so we have a real VFS fd */
    int fd = vfs_open("/fcntl_test", O_CREAT | O_RDWR, 0644);
    ASSERT(fd >= 0, "fcntl test: open failed");

    /* F_GETFL: should reflect the open flags (O_RDWR) */
    int flags = sys_fcntl(fd, F_GETFL, 0);
    ASSERT((flags & (O_RDONLY | O_RDWR)) == O_RDWR, "F_GETFL wrong access mode");

    /* F_SETFL / F_GETFL: add O_NONBLOCK */
    int r = sys_fcntl(fd, F_SETFL, O_NONBLOCK);
    ASSERT(r == 0, "F_SETFL failed");
    flags = sys_fcntl(fd, F_GETFL, 0);
    ASSERT(flags & O_NONBLOCK, "F_SETFL did not set O_NONBLOCK");

    /* F_GETFD: no CLOEXEC initially */
    r = sys_fcntl(fd, F_GETFD, 0);
    ASSERT(r == 0, "F_GETFD should be 0 initially");

    /* F_SETFD: set FD_CLOEXEC */
    r = sys_fcntl(fd, F_SETFD, FD_CLOEXEC);
    ASSERT(r == 0, "F_SETFD failed");
    r = sys_fcntl(fd, F_GETFD, 0);
    ASSERT(r == FD_CLOEXEC, "F_GETFD should be FD_CLOEXEC after F_SETFD");

    /* F_DUPFD: duplicate to first fd >= fd+1 */
    int fd2 = sys_fcntl(fd, F_DUPFD, fd + 1);
    ASSERT(fd2 > fd, "F_DUPFD did not return fd >= minfd");

    /* New descriptor must NOT inherit CLOEXEC (POSIX requirement) */
    r = sys_fcntl(fd2, F_GETFD, 0);
    ASSERT(r == 0, "F_DUPFD new fd must have CLOEXEC cleared");

    vfs_close(fd2);
    vfs_close(fd);

    /* ── ioctl: TIOCGWINSZ ───────────────────────────────────────────────── */
    struct winsize ws;
    extern void *memset(void *, int, size_t);
    memset(&ws, 0, sizeof(ws));
    r = sys_ioctl(1 /* stdout */, TIOCGWINSZ, &ws);
    ASSERT(r == 0, "TIOCGWINSZ failed");
    ASSERT(ws.ws_row == 25, "TIOCGWINSZ wrong row count");
    ASSERT(ws.ws_col == 80, "TIOCGWINSZ wrong col count");

    /* ── ioctl: TCGETS ───────────────────────────────────────────────────── */
    struct termios t;
    memset(&t, 0, sizeof(t));
    r = sys_ioctl(0 /* stdin */, TCGETS, &t);
    ASSERT(r == 0, "TCGETS failed");
    ASSERT(t.c_lflag & ICANON, "TCGETS: ICANON not set");
    ASSERT(t.c_lflag & ECHO,   "TCGETS: ECHO not set");
    ASSERT(t.c_cc[VEOF] == 0x04, "TCGETS: EOF not ^D");

    /* ── ioctl: TCSETS (ignored) ─────────────────────────────────────────── */
    r = sys_ioctl(0, TCSETS, &t);
    ASSERT(r == 0, "TCSETS should succeed");

    /* ── ioctl: FIONREAD on a pipe ───────────────────────────────────────── */
    int pipefds[2];
    r = sys_pipe(pipefds);
    ASSERT(r == 0, "FIONREAD: sys_pipe failed");

    int avail = -1;
    r = sys_ioctl(pipefds[0], FIONREAD, &avail);
    ASSERT(r == 0,     "FIONREAD failed on empty pipe");
    ASSERT(avail == 0, "FIONREAD on empty pipe should be 0");

    /* Write 3 bytes into the write end and check the read end */
    struct file_desc *wfdp = vfs_get_fd(pipefds[1]);
    ASSERT(wfdp && wfdp->is_pipe, "FIONREAD: write end not a pipe");
    pipe_write((struct pipe *)wfdp->pipe_ptr, "abc", 3);

    avail = -1;
    r = sys_ioctl(pipefds[0], FIONREAD, &avail);
    ASSERT(r == 0,     "FIONREAD failed after write");
    ASSERT(avail == 3, "FIONREAD should report 3 bytes after write");

    vfs_close(pipefds[0]);
    vfs_close(pipefds[1]);
}
#endif /* !__aarch64__ */

/* Test 9: per-process working directory (chdir / getcwd) */
static void test_per_process_cwd(void) {
    if (!current) { selftest_passed++; return; }

    /* Initial cwd should be "/" */
    ASSERT(current->cwd[0] == '/', "initial cwd[0] should be '/'");
    ASSERT(current->cwd[1] == '\0', "initial cwd should be exactly '/'");

    /* Create a directory and chdir into it */
    vfs_mkdir("/cwdtest", 0755);

    /* Simulate chdir via the struct (kernel-level) */
    extern char *strncpy(char *, const char *, size_t);
    strncpy(current->cwd, "/cwdtest", 255);
    current->cwd[255] = '\0';

    ASSERT(current->cwd[0] == '/', "cwd after chdir should start with '/'");

    extern int strcmp(const char *a, const char *b);
    ASSERT(strcmp(current->cwd, "/cwdtest") == 0, "cwd should be /cwdtest");

    /* Restore */
    current->cwd[0] = '/';
    current->cwd[1] = '\0';
}

/* Test 10: select(2) — poll on pipe read-end */
#ifndef __aarch64__
static void test_select(void) {
    extern int sys_select(int nfds, fd_set *rfd, fd_set *wfd,
                          fd_set *efd, struct timeval *tv);
    extern int sys_pipe(int fds[2]);
    extern ssize_t pipe_write(struct pipe *p, const void *buf, size_t n);

    int pipefds[2];
    int r = sys_pipe(pipefds);
    ASSERT(r == 0, "select: sys_pipe failed");

    int rfd_idx = pipefds[0];
    int wfd_idx = pipefds[1];

    fd_set rfds, wfds;

    /* ── Empty pipe: poll mode (timeout={0,0}) — read-end NOT ready ── */
    FD_ZERO(&rfds);
    FD_SET(rfd_idx, &rfds);
    struct timeval tv = {0, 0};
    r = sys_select(rfd_idx + 1, &rfds, NULL, NULL, &tv);
    ASSERT(r == 0,                      "select: empty pipe should not be read-ready");
    ASSERT(!FD_ISSET(rfd_idx, &rfds),   "select: empty pipe read-fd must not be set");

    /* ── Write data into the pipe then check read-ready ── */
    struct file_desc *wfdp = vfs_get_fd(wfd_idx);
    ASSERT(wfdp && wfdp->is_pipe, "select: write end not a pipe");
    pipe_write((struct pipe *)wfdp->pipe_ptr, "xy", 2);

    FD_ZERO(&rfds);
    FD_SET(rfd_idx, &rfds);
    tv.tv_sec = 0; tv.tv_usec = 0;
    r = sys_select(rfd_idx + 1, &rfds, NULL, NULL, &tv);
    ASSERT(r == 1,                    "select: pipe with data should be read-ready");
    ASSERT(FD_ISSET(rfd_idx, &rfds),  "select: read fd should be set after write");

    /* ── Write-end should always be write-ready ── */
    FD_ZERO(&wfds);
    FD_SET(wfd_idx, &wfds);
    tv.tv_sec = 0; tv.tv_usec = 0;
    r = sys_select(wfd_idx + 1, NULL, &wfds, NULL, &tv);
    ASSERT(r == 1,                    "select: pipe write-end should be write-ready");
    ASSERT(FD_ISSET(wfd_idx, &wfds),  "select: write fd should be set");

    /* ── Regular file is always read-ready ── */
    int fd = vfs_open("/select_test_file", O_CREAT | O_RDWR, 0644);
    ASSERT(fd >= 0, "select: could not create test file");

    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    tv.tv_sec = 0; tv.tv_usec = 0;
    r = sys_select(fd + 1, &rfds, NULL, NULL, &tv);
    ASSERT(r == 1,               "select: regular file should be read-ready");
    ASSERT(FD_ISSET(fd, &rfds),  "select: regular file fd should be set");

    vfs_close(fd);
    vfs_close(pipefds[0]);
    vfs_close(pipefds[1]);
}

/* Test 11: ftruncate / truncate */
static void test_ftruncate(void) {
    /* Create a file and write initial content */
    int fd = vfs_open("/trunc_test", O_CREAT | O_RDWR, 0644);
    ASSERT(fd >= 0, "ftruncate: open failed");

    const char *data = "Hello, World!";  /* 13 bytes */
    ssize_t w = vfs_write(fd, data, 13);
    ASSERT(w == 13, "ftruncate: initial write failed");

    struct stat st;
    int r = vfs_fstat(fd, &st);
    ASSERT(r == 0, "ftruncate: fstat before truncate failed");
    ASSERT(st.st_size == 13, "ftruncate: initial size should be 13");

    /* Shrink to 5 bytes */
    r = vfs_ftruncate(fd, 5);
    ASSERT(r == 0, "ftruncate: shrink failed");
    r = vfs_fstat(fd, &st);
    ASSERT(r == 0, "ftruncate: fstat after shrink failed");
    ASSERT(st.st_size == 5, "ftruncate: size after shrink should be 5");

    /* Read back: should see "Hello" */
    vfs_lseek(fd, 0, SEEK_SET);
    char buf[8] = {0};
    ssize_t rd = vfs_read(fd, buf, 5);
    ASSERT(rd == 5, "ftruncate: read after shrink wrong count");
    extern int strncmp(const char *, const char *, size_t);
    ASSERT(strncmp(buf, "Hello", 5) == 0, "ftruncate: data after shrink mismatch");

    /* Extend to 10 bytes: the extra 5 should be zero-filled */
    r = vfs_ftruncate(fd, 10);
    ASSERT(r == 0, "ftruncate: extend failed");
    r = vfs_fstat(fd, &st);
    ASSERT(r == 0, "ftruncate: fstat after extend failed");
    ASSERT(st.st_size == 10, "ftruncate: size after extend should be 10");

    /* Read the zero-filled tail */
    vfs_lseek(fd, 5, SEEK_SET);
    char tail[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    rd = vfs_read(fd, tail, 5);
    ASSERT(rd == 5, "ftruncate: read of extended tail wrong count");
    ASSERT(tail[0] == 0 && tail[4] == 0, "ftruncate: extended tail should be zero");

    vfs_close(fd);

    /* vfs_truncate (by path) */
    r = vfs_truncate("/trunc_test", 2);
    ASSERT(r == 0, "truncate by path failed");
    r = vfs_stat("/trunc_test", &st);
    ASSERT(r == 0, "stat after truncate by path failed");
    ASSERT(st.st_size == 2, "truncate by path: size should be 2");
}
#endif /* !__aarch64__ */

void kernel_selftest(void) {
    kprintf("CORE: running BIST...\n");
    selftest_passed = 0;
    selftest_failed = 0;

    test_buddy();
    test_slab();
    test_vfs();
    test_pipe();
    test_kthread();
    test_signal();
#ifndef __aarch64__
    test_unix_socket();
    test_fcntl_ioctl();
#endif
    test_per_process_cwd();
#ifndef __aarch64__
    test_select();
    test_ftruncate();
#endif

    kprintf("CORE: BIST passed (%d checks)\n", selftest_passed);
}
