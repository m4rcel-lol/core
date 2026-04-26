/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>
#include <core/proc.h>
#include <core/mm.h>
#include <core/drivers.h>

/* Round-robin scheduler with 3 priority queues */

extern struct proc *current;
extern void switch_context(struct proc *from, struct proc *to);

static struct proc *run_queue[PRIO_COUNT];
static struct proc *idle_proc;
static int sched_initialized = 0;
static int sched_started = 0;
static uint32_t slice_counter = 0;

#define TIME_SLICE_TICKS 1   /* 10 ms per tick = 10 ms slice */

void sched_init(void) {
    for (int i = 0; i < PRIO_COUNT; i++) run_queue[i] = NULL;
    sched_initialized = 1;
    sched_started = 0;
    slice_counter = TIME_SLICE_TICKS;
}

int sched_is_started(void) {
    return sched_started;
}

void sched_set_idle(struct proc *p) {
    idle_proc = p;
    current = p;
}

void sched_enqueue(struct proc *p) {
    if (!p) return;
    int prio = p->priority < PRIO_COUNT ? p->priority : PRIO_NORMAL;
    p->state = PROC_READY;
    p->next  = NULL;
    if (!run_queue[prio]) {
        run_queue[prio] = p;
    } else {
        struct proc *tail = run_queue[prio];
        while (tail->next) tail = tail->next;
        tail->next = p;
    }
}

static struct proc *pick_next(void) {
    for (int prio = 0; prio < PRIO_COUNT; prio++) {
        if (run_queue[prio]) {
            struct proc *p = run_queue[prio];
            run_queue[prio] = p->next;
            p->next = NULL;
            return p;
        }
    }
    return idle_proc;
}

void sched_yield(void) {
    if (!sched_initialized || !sched_started) return;
    struct proc *prev = current;
    struct proc *next = pick_next();
    if (next == prev) return;
    if (prev && prev != idle_proc && prev->state == PROC_RUNNING) {
        prev->state = PROC_READY;
        sched_enqueue(prev);
    }
    if (next) {
        next->state = PROC_RUNNING;
        current = next;

#ifndef __aarch64__
        /* FPU lazy save: if the outgoing task used FPU, save its state */
        if (prev && prev->fpu_used) {
            uint8_t *fpu = proc_fpu_state(prev);
            __asm__ volatile("fxsave (%0)" : : "r"(fpu) : "memory");
        }
        /* Set CR0.TS so the next task's first FPU use triggers #NM */
        uint64_t cr0;
        __asm__ volatile("movq %%cr0, %0" : "=r"(cr0));
        cr0 |= (1ULL << 3);
        __asm__ volatile("movq %0, %%cr0" : : "r"(cr0) : "memory");

        /* Switch page tables */
        if (next->pml4) {
            uint64_t phys = VIRT_TO_PHYS((uint64_t)next->pml4);
            __asm__ volatile("movq %0, %%cr3" : : "r"(phys) : "memory");
        }
#endif /* !__aarch64__ */

        switch_context(prev, next);
    }
}

void sched_tick(void) {
    if (!sched_initialized || !sched_started) return;
    if (slice_counter > 0) slice_counter--;
    if (slice_counter == 0) {
        slice_counter = TIME_SLICE_TICKS;
        if (current && current->state == PROC_RUNNING) {
            sched_yield();
        }
    }
}

void sched_start(void) {
    sched_started = 1;
    /* Enable interrupts and enter idle loop */
#ifdef __aarch64__
    __asm__ volatile("msr daifclr, #2");   /* clear IRQ mask → enable IRQs */
    while (1) __asm__ volatile("wfe");
#else
    __asm__ volatile("sti");
    while (1) {
        __asm__ volatile("hlt");
        /* When we return from halt, a timer IRQ fired and sched_tick ran */
    }
#endif
}

void sched_block(struct proc *p) {
    if (!p) return;
    p->state = PROC_BLOCKED;
}

void sched_unblock(struct proc *p) {
    if (!p || p->state != PROC_BLOCKED) return;
    sched_enqueue(p);
}
