/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>
#include <core/signal.h>
#include <core/proc.h>
#include <core/vfs.h>
#include <core/drivers.h>

extern struct proc *current;
extern struct proc *proc_get_by_pid(pid_t pid);

void signal_init(void) {
    /* Nothing to initialize globally */
}

int signal_send(pid_t pid, int sig) {
    if (sig < 1 || sig > NSIG) return -EINVAL;
    struct proc *p = proc_get_by_pid(pid);
    if (!p) return -ESRCH;
    if (p->sigmask & (1ULL << (sig - 1))) return 0; /* blocked */
    p->sigpending |= (1ULL << (sig - 1));
    return 0;
}

void signal_deliver(void) {
    if (!current) return;
    uint64_t pending = current->sigpending & ~current->sigmask;
    if (!pending) return;

    for (int sig = 1; sig <= NSIG; sig++) {
        if (pending & (1ULL << (sig - 1))) {
            current->sigpending &= ~(1ULL << (sig - 1));
            struct sigaction *sa = &current->sighandlers[sig - 1];
            if (sa->sa_handler == SIG_IGN) continue;
            if (sa->sa_handler == SIG_DFL || !sa->sa_handler) {
                /* Default actions */
                if (sig == SIGKILL || sig == SIGTERM || sig == SIGINT) {
                    extern void sys_exit(int code);
                    sys_exit(sig);
                }
                continue;
            }
            /* Call user handler — simplified: call directly in kernel context */
            sa->sa_handler(sig);
            if (sa->sa_flags & SA_RESETHAND) sa->sa_handler = SIG_DFL;
        }
    }
}

int sys_sigaction(int sig, const struct sigaction *act, struct sigaction *old) {
    if (sig < 1 || sig > NSIG) return -EINVAL;
    if (sig == SIGKILL || sig == SIGSTOP) return -EPERM;
    if (!current) return -EINVAL;
    if (old) *old = current->sighandlers[sig - 1];
    if (act) current->sighandlers[sig - 1] = *act;
    return 0;
}

int sys_sigreturn(void) {
    /* In a real kernel, this would restore user context from signal stack frame */
    return 0;
}

int sys_kill(pid_t pid, int sig) {
    return signal_send(pid, sig);
}
