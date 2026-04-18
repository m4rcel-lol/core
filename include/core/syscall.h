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
#define SYS_POLL        43
#define SYS_GETUID      44
#define SYS_GETEUID     45
#define SYS_GETGID      46
#define SYS_GETEGID     47
#define SYS_SETUID      48
#define SYS_SETGID      49
#define SYS_CHMOD       50
#define SYS_CHOWN       51
#define SYS_ACCESS      52
#define SYS_ALARM       53
#define SYS_NANOSLEEP   54
#define SYS_SETSID      55
#define SYS_GETPGRP     56
#define SYS_SETPGID     57
#define SYS_LINK        58
#define SYS_SYMLINK     59
#define SYS_READLINK    60
#define SYS_SYSINFO     61
#define SYS_UMASK       62
#define SYSCALL_COUNT   63

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

/*
 * struct pollfd for poll(2)
 */
struct pollfd {
    int     fd;
    int16_t events;
    int16_t revents;
};

/* poll(2) event bits */
#define POLLIN    0x0001   /* data to read */
#define POLLPRI   0x0002   /* urgent/priority data */
#define POLLOUT   0x0004   /* ready to write */
#define POLLERR   0x0008   /* error (output only) */
#define POLLHUP   0x0010   /* hangup (output only) */
#define POLLNVAL  0x0020   /* invalid fd (output only) */
#define POLLRDNORM 0x0040  /* normal data available (= POLLIN) */
#define POLLWRNORM 0x0100  /* normal data writable (= POLLOUT) */

/*
 * struct timespec for nanosleep(2)
 */
struct timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

/*
 * struct sysinfo for sysinfo(2)
 */
struct sysinfo {
    int64_t  uptime;         /* seconds since boot */
    uint64_t loads[3];       /* 1/5/15-min load averages × 65536 (stub: 0) */
    uint64_t totalram;       /* total usable RAM in bytes */
    uint64_t freeram;        /* available RAM in bytes */
    uint64_t sharedram;      /* shared memory (stub: 0) */
    uint64_t bufferram;      /* buffer memory (stub: 0) */
    uint64_t totalswap;      /* total swap (stub: 0) */
    uint64_t freeswap;       /* free swap (stub: 0) */
    uint16_t procs;          /* number of running processes */
    uint16_t pad;
    uint32_t pad2;
    uint64_t totalhigh;      /* stub: 0 */
    uint64_t freehigh;       /* stub: 0 */
    uint32_t mem_unit;       /* memory unit size in bytes (4096) */
};

#endif /* CORE_SYSCALL_H */
