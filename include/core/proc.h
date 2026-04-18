/* CORE Kernel — (c) CORE Project, MIT License */
#ifndef CORE_PROC_H
#define CORE_PROC_H

#include <core/types.h>
#include <core/signal.h>

#define PROC_MAX      256
#define FD_MAX        64
#define PROC_NAME_LEN 32

enum proc_state {
    PROC_RUNNING  = 0,
    PROC_READY    = 1,
    PROC_BLOCKED  = 2,
    PROC_ZOMBIE   = 3,
    PROC_DEAD     = 4
};

enum proc_priority {
    PRIO_REALTIME = 0,
    PRIO_NORMAL   = 1,
    PRIO_IDLE     = 2,
    PRIO_COUNT    = 3
};

struct regs {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp;
    uint64_t r8,  r9,  r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rsp;
    uint64_t rip;
    uint64_t rflags;
};

struct proc {
    uint32_t         pid;
    uint32_t         ppid;
    enum proc_state  state;
    struct regs      ctx;
    uint64_t        *pml4;
    int              fds[FD_MAX];
    uint64_t         sigmask;
    uint64_t         sigpending;
    struct sigaction sighandlers[64];
    int              exit_code;
    uint8_t          priority;
    uint8_t          fpu_used;    /* lazy FPU: nonzero when FPU state is valid */
    char             name[PROC_NAME_LEN];
    char             cwd[256];           /* per-process working directory */
    struct proc     *next;
    uint8_t          kstack[4096 - sizeof(uint32_t)*2 - sizeof(enum proc_state)
                            - sizeof(struct regs) - sizeof(uint64_t *)
                            - sizeof(int)*FD_MAX - sizeof(uint64_t)*2
                            - sizeof(struct sigaction)*64 - sizeof(int)
                            - sizeof(uint8_t)*2 - PROC_NAME_LEN
                            - 256 /* cwd */
                            - sizeof(struct proc *)];
};

void proc_init(void);
struct proc *proc_alloc(void);
void proc_free(struct proc *p);
int  sys_fork(void);
void sys_exit(int code);
int  sys_wait(int *status);
int  proc_execve(const char *path, char *argv[], char *envp[]);
int  proc_kthread(void (*fn)(void *), void *arg);
uint8_t *proc_fpu_state(struct proc *p);

extern struct proc *current;

#endif /* CORE_PROC_H */
