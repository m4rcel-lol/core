/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>
#include <core/proc.h>
#include <core/drivers.h>

/* Round-robin scheduler with 3 priority queues */

extern struct proc *current;
extern void switch_context(struct proc *from, struct proc *to);

static struct proc *run_queue[PRIO_COUNT];
static struct proc *idle_proc;
static int sched_initialized = 0;
static uint32_t slice_counter = 0;

#define TIME_SLICE_TICKS 1   /* 10 ms per tick = 10 ms slice */

void sched_init(void) {
    for (int i = 0; i < PRIO_COUNT; i++) run_queue[i] = NULL;
    sched_initialized = 1;
    slice_counter = TIME_SLICE_TICKS;
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
    if (!sched_initialized) return;
    struct proc *prev = current;
    struct proc *next = pick_next();
    if (next == prev) return;
    if (prev && prev->state == PROC_RUNNING) {
        prev->state = PROC_READY;
        sched_enqueue(prev);
    }
    if (next) {
        next->state = PROC_RUNNING;
        current = next;
        /* Switch page tables */
        if (next->pml4) {
            extern uint64_t vmm_get_kernel_pml4(void);
            extern uint64_t VIRT_TO_PHYS_fn(uint64_t);
            uint64_t phys = (uint64_t)next->pml4 - 0xFFFFFFFF80000000ULL;
            __asm__ volatile("movq %0, %%cr3" : : "r"(phys) : "memory");
        }
        switch_context(prev, next);
    }
}

void sched_tick(void) {
    if (!sched_initialized) return;
    if (slice_counter > 0) slice_counter--;
    if (slice_counter == 0) {
        slice_counter = TIME_SLICE_TICKS;
        if (current && current->state == PROC_RUNNING) {
            sched_yield();
        }
    }
}

void sched_start(void) {
    /* Enable interrupts and enter idle loop */
    __asm__ volatile("sti");
    while (1) {
        __asm__ volatile("hlt");
        /* When we return from halt, a timer IRQ fired and sched_tick ran */
    }
}

void sched_block(struct proc *p) {
    if (!p) return;
    p->state = PROC_BLOCKED;
}

void sched_unblock(struct proc *p) {
    if (!p || p->state != PROC_BLOCKED) return;
    sched_enqueue(p);
}
