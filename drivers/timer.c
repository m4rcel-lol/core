/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>
#include <core/drivers.h>
#include <core/proc.h>

/* PIT 8253/8254 — Channel 0, Mode 2, 100 Hz */
#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_FREQ     1193182
#define PIT_HZ       100
#define PIT_DIVISOR  (PIT_FREQ / PIT_HZ)

static volatile uint64_t jiffies = 0;

/* Processes sleeping, indexed by jiffie wake time */
#define SLEEP_MAX 64
struct sleep_entry {
    struct proc *proc;
    uint64_t wake_at;
    int      in_use;
};
static struct sleep_entry sleep_table[SLEEP_MAX];

void timer_init(void) {
    uint16_t divisor = (uint16_t)PIT_DIVISOR;
    io_outb(PIT_COMMAND, 0x34);   /* channel 0, lo/hi byte, mode 2 */
    io_outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    io_outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));
    jiffies = 0;
    for (int i = 0; i < SLEEP_MAX; i++) sleep_table[i].in_use = 0;
}

void timer_irq_handler(void) {
    jiffies++;
    uint64_t now_ms = jiffies * 10;

    /* Wake sleeping processes */
    for (int i = 0; i < SLEEP_MAX; i++) {
        if (sleep_table[i].in_use && jiffies >= sleep_table[i].wake_at) {
            sleep_table[i].in_use = 0;
            if (sleep_table[i].proc) {
                extern void sched_unblock(struct proc *p);
                sched_unblock(sleep_table[i].proc);
            }
        }
    }

    /* Check SIGALRM deadlines for all processes */
    proc_check_alarms(now_ms);

    extern void sched_tick(void);
    sched_tick();
}

uint64_t kernel_uptime_ms(void) {
    return jiffies * 10;   /* 100 Hz → 10 ms per tick */
}

static void timer_spin_delay_ms(uint32_t ms) {
    volatile uint64_t loops = (uint64_t)ms * 10000ULL;
    while (loops--) {
        __asm__ volatile("pause");
    }
}

void timer_sleep_ms(uint32_t ms) {
    extern int sched_is_started(void);

    if (!current || !sched_is_started()) {
        timer_spin_delay_ms(ms);
        return;
    }

    uint64_t ticks = (uint64_t)ms / 10 + 1;
    uint64_t wake  = jiffies + ticks;
    /* Find a free sleep slot */
    for (int i = 0; i < SLEEP_MAX; i++) {
        if (!sleep_table[i].in_use) {
            sleep_table[i].in_use  = 1;
            sleep_table[i].proc    = current;
            sleep_table[i].wake_at = wake;
            extern void sched_block(struct proc *p);
            sched_block(current);
            extern void sched_yield(void);
            sched_yield();
            return;
        }
    }
    /* No slot: busy-wait */
    while (jiffies < wake) __asm__ volatile("pause");
}
