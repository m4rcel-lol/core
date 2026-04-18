/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>
#include <core/proc.h>
#include <core/mm.h>
#include <core/elf.h>
#include <core/drivers.h>
#include <core/vfs.h>

static struct proc proc_table[PROC_MAX];
static uint8_t     proc_used[PROC_MAX];

/* 512-byte FXSAVE buffers, one per process slot, 16-byte aligned */
static uint8_t fpu_state_pool[PROC_MAX][512] __attribute__((aligned(16)));

struct proc *current = NULL;
static uint32_t next_pid = 0;

extern uint64_t *vmm_get_kernel_pml4(void);
extern uint64_t *vmm_fork(uint64_t *pml4);

uint8_t *proc_fpu_state(struct proc *p) {
    int idx = (int)(p - proc_table);
    if (idx < 0 || idx >= PROC_MAX) return fpu_state_pool[0];
    return fpu_state_pool[idx];
}

void proc_init(void) {
    for (int i = 0; i < PROC_MAX; i++) proc_used[i] = 0;
    next_pid = 0;
}

struct proc *proc_alloc(void) {
    for (int i = 0; i < PROC_MAX; i++) {
        if (!proc_used[i]) {
            proc_used[i] = 1;
            struct proc *p = &proc_table[i];
            /* Zero-initialize */
            uint8_t *b = (uint8_t *)p;
            for (size_t j = 0; j < sizeof(struct proc); j++) b[j] = 0;
            p->pid   = next_pid++;
            p->ppid  = 0;
            p->state = PROC_READY;
            p->priority = PRIO_NORMAL;
            p->pml4  = vmm_get_kernel_pml4();
            for (int fd = 0; fd < FD_MAX; fd++) p->fds[fd] = -1;
            p->cwd[0] = '/';
            p->cwd[1] = '\0';
            return p;
        }
    }
    return NULL;
}

void proc_free(struct proc *p) {
    if (!p) return;
    int idx = (int)(p - proc_table);
    if (idx >= 0 && idx < PROC_MAX) {
        proc_used[idx] = 0;
        p->state = PROC_DEAD;
    }
}

extern void switch_context(struct proc *from, struct proc *to);
extern void sched_enqueue(struct proc *p);

struct proc *proc_get_by_pid(pid_t pid) {
    for (int i = 0; i < PROC_MAX; i++) {
        if (proc_used[i] && proc_table[i].pid == (uint32_t)pid)
            return &proc_table[i];
    }
    return NULL;
}

int sys_fork(void) {
    if (!current) return -1;
    struct proc *child = proc_alloc();
    if (!child) return -ENOMEM;

    /* Copy context from parent */
    child->ctx   = current->ctx;
    child->ppid  = current->pid;
    child->priority = current->priority;
    child->sigmask = current->sigmask;

    /* Copy name */
    for (int i = 0; i < PROC_NAME_LEN; i++) child->name[i] = current->name[i];

    /* Copy working directory */
    for (int i = 0; i < 256; i++) child->cwd[i] = current->cwd[i];

    /* Copy file descriptors */
    for (int fd = 0; fd < FD_MAX; fd++) child->fds[fd] = current->fds[fd];

    /* Fork page tables (COW) */
    if (current->pml4) {
        child->pml4 = vmm_fork(current->pml4);
    }

    /* Child returns 0 from fork */
    child->ctx.rax = 0;
    child->state   = PROC_READY;
    sched_enqueue(child);

    /* Parent returns child pid */
    return (int)child->pid;
}

void sys_exit(int code) {
    if (!current) return;
    current->exit_code = code;
    current->state = PROC_ZOMBIE;

    /* Wake up parent if it's waiting */
    if (current->ppid > 0) {
        struct proc *parent = proc_get_by_pid((pid_t)current->ppid);
        if (parent && parent->state == PROC_BLOCKED) {
            parent->state = PROC_READY;
            sched_enqueue(parent);
        }
    }

    /* Signal SIGCHLD to parent */
    if (current->ppid > 0) {
        struct proc *parent = proc_get_by_pid((pid_t)current->ppid);
        if (parent) parent->sigpending |= (1ULL << (SIGCHLD - 1));
    }

    /* Yield — scheduler will not reschedule ZOMBIE processes */
    extern void sched_yield(void);
    sched_yield();
}

int sys_wait(int *status) {
    if (!current) return -ECHILD;
    /* Look for zombie children */
    for (;;) {
        for (int i = 0; i < PROC_MAX; i++) {
            if (proc_used[i] &&
                proc_table[i].ppid == current->pid &&
                proc_table[i].state == PROC_ZOMBIE) {
                struct proc *child = &proc_table[i];
                pid_t cpid = (pid_t)child->pid;
                if (status) *status = child->exit_code;
                proc_free(child);
                return (int)cpid;
            }
        }
        /* No zombie yet, block */
        current->state = PROC_BLOCKED;
        extern void sched_yield(void);
        sched_yield();
    }
}

extern void kprintf(const char *fmt, ...);
extern size_t strlen(const char *s);
extern void *memset(void *, int, size_t);
extern char *strncpy(char *, const char *, size_t);

int proc_execve(const char *path, char *argv[], char *envp[]) {
    /* Attempt to load the binary as an ELF64 executable */
    uint64_t *pml4 = NULL;
    uint64_t  sp   = 0;
    uint64_t  entry = elf_load(path, argv, envp, &pml4, &sp);
    if (!entry || !pml4) {
        kprintf("CORE: exec(%s) failed: not a valid ELF64\n", path);
        return -ENOEXEC;
    }

    struct proc *p = proc_alloc();
    if (!p) return -ENOMEM;

    strncpy(p->name, path, PROC_NAME_LEN - 1);
    p->priority  = PRIO_NORMAL;
    p->state     = PROC_READY;
    p->pml4      = pml4;
    p->fpu_used  = 0;

    /* Zero all saved registers, then set entry and stack */
    uint8_t *b = (uint8_t *)&p->ctx;
    for (size_t i = 0; i < sizeof(p->ctx); i++) b[i] = 0;
    p->ctx.rip    = entry;
    p->ctx.rsp    = sp;
    p->ctx.rflags = 0x202ULL;   /* IF=1, reserved bit 1 */

    sched_enqueue(p);
    kprintf("CORE: PID %u exec(%s) entry=0x%llx\n",
            p->pid, path, (unsigned long long)entry);
    return (int)p->pid;
}

int proc_kthread(void (*fn)(void *), void *arg) {
    struct proc *p = proc_alloc();
    if (!p) return -ENOMEM;
    /* Allocate a kernel stack page */
    void *stack_phys = pmm_alloc(0);
    if (!stack_phys) { proc_free(p); return -ENOMEM; }
    uint64_t stack_top = (uint64_t)PHYS_TO_VIRT(stack_phys) + PAGE_SIZE;
    /* Set up stack with fn and arg for a simple trampoline */
    stack_top -= sizeof(uint64_t);
    *(uint64_t *)stack_top = (uint64_t)arg;
    p->ctx.rsp = stack_top;
    p->ctx.rip = (uint64_t)fn;
    p->ctx.rdi = (uint64_t)arg;
    p->priority = PRIO_NORMAL;
    p->state    = PROC_READY;
    p->pml4     = vmm_get_kernel_pml4();
    sched_enqueue(p);
    return (int)p->pid;
}
