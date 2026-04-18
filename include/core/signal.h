/* CORE Kernel — (c) CORE Project, MIT License */
#ifndef CORE_SIGNAL_H
#define CORE_SIGNAL_H

#include <core/types.h>

#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGILL   4
#define SIGTRAP  5
#define SIGABRT  6
#define SIGBUS   7
#define SIGFPE   8
#define SIGKILL  9
#define SIGUSR1  10
#define SIGSEGV  11
#define SIGUSR2  12
#define SIGPIPE  13
#define SIGALRM  14
#define SIGTERM  15
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19
#define SIGTSTP  20
#define SIGTTIN  21
#define SIGTTOU  22
#define NSIG     64

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)

#define SA_NOCLDSTOP 0x00000001
#define SA_NOCLDWAIT 0x00000002
#define SA_SIGINFO   0x00000004
#define SA_RESTART   0x10000000
#define SA_NODEFER   0x40000000
#define SA_RESETHAND 0x80000000

struct sigaction {
    void     (*sa_handler)(int);
    uint64_t   sa_mask;
    uint32_t   sa_flags;
};

void signal_init(void);
int  signal_send(pid_t pid, int sig);
void signal_deliver(void);
int  sys_sigaction(int sig, const struct sigaction *act, struct sigaction *old);
int  sys_sigreturn(void);
int  sys_kill(pid_t pid, int sig);

#endif /* CORE_SIGNAL_H */
