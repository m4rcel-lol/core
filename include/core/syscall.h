/* CORE Kernel — (c) CORE Project, MIT License */
#ifndef CORE_SYSCALL_H
#define CORE_SYSCALL_H

#include <core/types.h>

#define SYS_EXIT        0
#define SYS_FORK        1
#define SYS_EXEC        2
#define SYS_WAIT        3
#define SYS_GETPID      4
#define SYS_GETPPID     5
#define SYS_KILL        6
#define SYS_SIGACTION   7
#define SYS_SIGRETURN   8
#define SYS_SLEEP       9
#define SYS_OPEN        10
#define SYS_CLOSE       11
#define SYS_READ        12
#define SYS_WRITE       13
#define SYS_LSEEK       14
#define SYS_FSTAT       15
#define SYS_STAT        16
#define SYS_DUP         17
#define SYS_DUP2        18
#define SYS_PIPE        19
#define SYS_MKDIR       20
#define SYS_RMDIR       21
#define SYS_UNLINK      22
#define SYS_RENAME      23
#define SYS_CHDIR       24
#define SYS_GETCWD      25
#define SYS_READDIR     26
#define SYS_MMAP        27
#define SYS_MUNMAP      28
#define SYS_BRK         29
#define SYS_SOCKET      30
#define SYS_BIND        31
#define SYS_CONNECT     32
#define SYS_SEND        33
#define SYS_RECV        34
#define SYS_UNAME       35
#define SYS_GETTIMEOFDAY 36
#define SYS_IOCTL       37
#define SYS_FCNTL       38
#define SYS_REBOOT      39
#define SYS_SELECT      40
#define SYS_FTRUNCATE   41
#define SYS_TRUNCATE    42
#define SYSCALL_COUNT   43

struct regs;

typedef uint64_t (*syscall_fn_t)(struct regs *r);

void     syscall_init(void);
uint64_t syscall_dispatch(struct regs *r);

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

struct timeval {
    int64_t tv_sec;
    int64_t tv_usec;
};

struct sockaddr {
    uint16_t sa_family;
    char     sa_data[14];
};

#define UNIX_PATH_MAX 108
struct sockaddr_un {
    uint16_t sun_family;              /* AF_UNIX */
    char     sun_path[UNIX_PATH_MAX]; /* socket path */
};

#define AF_UNIX  1
#define SOCK_STREAM 1
#define SOCK_DGRAM  2

#define REBOOT_MAGIC 0xDEADBEEF

/*
 * fd_set for select(2).
 * Supports up to FD_SETSIZE file descriptors (one bit per fd).
 * FD_SETSIZE must be a multiple of 64.
 */
#define FD_SETSIZE 256

typedef struct {
    uint64_t bits[FD_SETSIZE / 64];   /* 4 uint64_t = 256 bits */
} fd_set;

/* fd_set bit manipulation macros */
#define FD_ZERO(s)    do { \
    for (int _i = 0; _i < (int)(FD_SETSIZE/64); _i++) (s)->bits[_i] = 0; \
} while (0)
#define FD_SET(fd, s)   ((s)->bits[(fd)/64] |=  (1ULL << ((fd) % 64)))
#define FD_CLR(fd, s)   ((s)->bits[(fd)/64] &= ~(1ULL << ((fd) % 64)))
#define FD_ISSET(fd, s) (!!((s)->bits[(fd)/64] & (1ULL << ((fd) % 64))))

#endif /* CORE_SYSCALL_H */
