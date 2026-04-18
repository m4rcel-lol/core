/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>
#include <core/drivers.h>

#define IDT_ENTRIES 256
#define IDT_FLAG_INTERRUPT 0x8E  /* P=1, DPL=0, type=interrupt gate */
#define IDT_FLAG_TRAP      0x8F  /* P=1, DPL=0, type=trap gate */
#define IDT_FLAG_USER_INT  0xEE  /* P=1, DPL=3, type=interrupt gate */

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  flags;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr   idt_ptr;

extern void *isr_table[];

struct int_frame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

static void idt_set_gate(int vec, void *handler, uint8_t flags, uint8_t ist) {
    uint64_t addr = (uint64_t)handler;
    idt[vec].offset_low  = (uint16_t)(addr & 0xFFFF);
    idt[vec].selector    = 0x08;
    idt[vec].ist         = ist;
    idt[vec].flags       = flags;
    idt[vec].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
    idt[vec].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt[vec].reserved    = 0;
}

/* Exception names for panic messages */
static const char *exception_names[] = {
    "Division by Zero",           /* 0 */
    "Debug",                      /* 1 */
    "NMI",                        /* 2 */
    "Breakpoint",                 /* 3 */
    "Overflow",                   /* 4 */
    "Bound Range Exceeded",       /* 5 */
    "Invalid Opcode",             /* 6 */
    "Device Not Available",       /* 7 */
    "Double Fault",               /* 8 */
    "Coprocessor Segment Overrun",/* 9 */
    "Invalid TSS",                /* 10 */
    "Segment Not Present",        /* 11 */
    "Stack-Segment Fault",        /* 12 */
    "General Protection Fault",   /* 13 */
    "Page Fault",                 /* 14 */
    "Reserved",                   /* 15 */
    "x87 FPU Exception",          /* 16 */
    "Alignment Check",            /* 17 */
    "Machine Check",              /* 18 */
    "SIMD Float Exception",       /* 19 */
    "Virtualization Exception",   /* 20 */
    "Control Protection",         /* 21 */
    "Reserved",                   /* 22 */
    "Reserved",                   /* 23 */
    "Reserved",                   /* 24 */
    "Reserved",                   /* 25 */
    "Reserved",                   /* 26 */
    "Reserved",                   /* 27 */
    "Hypervisor Injection",       /* 28 */
    "VMM Communication",          /* 29 */
    "Security Exception",         /* 30 */
    "Reserved"                    /* 31 */
};

static volatile uint64_t g_cr2;

void exception_handler(uint64_t vec, uint64_t err, struct int_frame *frame) {
    const char *name = (vec < 32) ? exception_names[vec] : "Unknown";
    if (vec == 14) {
        /* Page fault — read CR2 */
        __asm__ volatile("movq %%cr2, %0" : "=r"(g_cr2));
        kprintf("PAGE FAULT: addr=0x%llx err=0x%llx rip=0x%llx\n",
                (unsigned long long)g_cr2,
                (unsigned long long)err,
                (unsigned long long)frame->rip);
    } else {
        kprintf("EXCEPTION %llu (%s): err=0x%llx rip=0x%llx\n",
                (unsigned long long)vec, name,
                (unsigned long long)err,
                (unsigned long long)frame->rip);
    }
    __asm__ volatile("cli; hlt");
    while(1) {}
}

/* IRQ handler — called from assembly stubs */
void irq_handler(uint64_t irq);

/* PIC ports */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

void pic_init(void) {
    /* ICW1: start init, expect ICW4 */
    io_outb(PIC1_CMD, 0x11);
    io_outb(PIC2_CMD, 0x11);
    io_wait();
    /* ICW2: vector offsets */
    io_outb(PIC1_DATA, 0x20);  /* IRQ0-7 → INT 32-39 */
    io_outb(PIC2_DATA, 0x28);  /* IRQ8-15 → INT 40-47 */
    io_wait();
    /* ICW3: cascade */
    io_outb(PIC1_DATA, 0x04);
    io_outb(PIC2_DATA, 0x02);
    io_wait();
    /* ICW4: 8086 mode */
    io_outb(PIC1_DATA, 0x01);
    io_outb(PIC2_DATA, 0x01);
    io_wait();
    /* Mask all except IRQ0 (timer) and IRQ1 (keyboard) */
    io_outb(PIC1_DATA, 0xFC);  /* unmask IRQ0, IRQ1 */
    io_outb(PIC2_DATA, 0xFF);
}

void pic_eoi(uint8_t irq) {
    if (irq >= 8) io_outb(PIC2_CMD, 0x20);
    io_outb(PIC1_CMD, 0x20);
}

/* Forward declarations for IRQ handlers */
void timer_irq_handler(void);
void keyboard_irq_handler(void);
void sched_tick(void);

void irq_handler(uint64_t irq) {
    switch (irq) {
    case 0: timer_irq_handler(); break;
    case 1: keyboard_irq_handler(); break;
    default: break;
    }
    pic_eoi((uint8_t)irq);
}

/* ISR stubs — defined in assembly */
#define ISR_NOERR(n) \
    void isr_##n##_stub(void); \
    __asm__( \
        ".global isr_" #n "_stub\n\t" \
        "isr_" #n "_stub:\n\t" \
        "pushq $0\n\t" \
        "pushq $" #n "\n\t" \
        "jmp isr_common\n\t" \
    );

#define ISR_ERR(n) \
    void isr_##n##_stub(void); \
    __asm__( \
        ".global isr_" #n "_stub\n\t" \
        "isr_" #n "_stub:\n\t" \
        "pushq $" #n "\n\t" \
        "jmp isr_common\n\t" \
    );

#define IRQ_STUB(n, irq) \
    void isr_##n##_stub(void); \
    __asm__( \
        ".global isr_" #n "_stub\n\t" \
        "isr_" #n "_stub:\n\t" \
        "pushq $0\n\t" \
        "pushq $" #n "\n\t" \
        "jmp irq_common\n\t" \
    );

ISR_NOERR(0)  ISR_NOERR(1)  ISR_NOERR(2)  ISR_NOERR(3)
ISR_NOERR(4)  ISR_NOERR(5)  ISR_NOERR(6)  ISR_NOERR(7)
ISR_ERR(8)    ISR_NOERR(9)  ISR_ERR(10)   ISR_ERR(11)
ISR_ERR(12)   ISR_ERR(13)   ISR_ERR(14)   ISR_NOERR(15)
ISR_NOERR(16) ISR_ERR(17)   ISR_NOERR(18) ISR_NOERR(19)
ISR_NOERR(20) ISR_NOERR(21) ISR_NOERR(22) ISR_NOERR(23)
ISR_NOERR(24) ISR_NOERR(25) ISR_NOERR(26) ISR_NOERR(27)
ISR_NOERR(28) ISR_NOERR(29) ISR_NOERR(30) ISR_NOERR(31)
IRQ_STUB(32, 0)  IRQ_STUB(33, 1)  IRQ_STUB(34, 2)  IRQ_STUB(35, 3)
IRQ_STUB(36, 4)  IRQ_STUB(37, 5)  IRQ_STUB(38, 6)  IRQ_STUB(39, 7)
IRQ_STUB(40, 8)  IRQ_STUB(41, 9)  IRQ_STUB(42, 10) IRQ_STUB(43, 11)
IRQ_STUB(44, 12) IRQ_STUB(45, 13) IRQ_STUB(46, 14) IRQ_STUB(47, 15)

__asm__(
    ".global isr_common\n\t"
    "isr_common:\n\t"
    /* Stack: [rip, cs, rflags, rsp, ss, vec, err] */
    "pushq %rax\n\t"
    "pushq %rbx\n\t"
    "pushq %rcx\n\t"
    "pushq %rdx\n\t"
    "pushq %rsi\n\t"
    "pushq %rdi\n\t"
    "pushq %rbp\n\t"
    "pushq %r8\n\t"
    "pushq %r9\n\t"
    "pushq %r10\n\t"
    "pushq %r11\n\t"
    "pushq %r12\n\t"
    "pushq %r13\n\t"
    "pushq %r14\n\t"
    "pushq %r15\n\t"
    /* arg0 = vec, arg1 = err, arg2 = &int_frame */
    "movq 15*8(%rsp), %rdi\n\t"   /* vec */
    "movq 16*8(%rsp), %rsi\n\t"   /* err */
    "leaq 17*8(%rsp), %rdx\n\t"   /* &int_frame */
    "call exception_handler\n\t"
    "popq %r15\n\t"
    "popq %r14\n\t"
    "popq %r13\n\t"
    "popq %r12\n\t"
    "popq %r11\n\t"
    "popq %r10\n\t"
    "popq %r9\n\t"
    "popq %r8\n\t"
    "popq %rbp\n\t"
    "popq %rdi\n\t"
    "popq %rsi\n\t"
    "popq %rdx\n\t"
    "popq %rcx\n\t"
    "popq %rbx\n\t"
    "popq %rax\n\t"
    "addq $16, %rsp\n\t"   /* pop vec + err */
    "iretq\n\t"
);

__asm__(
    ".global irq_common\n\t"
    "irq_common:\n\t"
    "pushq %rax\n\t"
    "pushq %rbx\n\t"
    "pushq %rcx\n\t"
    "pushq %rdx\n\t"
    "pushq %rsi\n\t"
    "pushq %rdi\n\t"
    "pushq %rbp\n\t"
    "pushq %r8\n\t"
    "pushq %r9\n\t"
    "pushq %r10\n\t"
    "pushq %r11\n\t"
    "pushq %r12\n\t"
    "pushq %r13\n\t"
    "pushq %r14\n\t"
    "pushq %r15\n\t"
    /* vec is at 15*8(%rsp); irq = vec - 32 */
    "movq 15*8(%rsp), %rdi\n\t"
    "subq $32, %rdi\n\t"
    "call irq_handler\n\t"
    "popq %r15\n\t"
    "popq %r14\n\t"
    "popq %r13\n\t"
    "popq %r12\n\t"
    "popq %r11\n\t"
    "popq %r10\n\t"
    "popq %r9\n\t"
    "popq %r8\n\t"
    "popq %rbp\n\t"
    "popq %rdi\n\t"
    "popq %rsi\n\t"
    "popq %rdx\n\t"
    "popq %rcx\n\t"
    "popq %rbx\n\t"
    "popq %rax\n\t"
    "addq $16, %rsp\n\t"
    "iretq\n\t"
);

void idt_init(void) {
    idt_set_gate(0,  isr_0_stub,  IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(1,  isr_1_stub,  IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(2,  isr_2_stub,  IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(3,  isr_3_stub,  IDT_FLAG_TRAP,      0);
    idt_set_gate(4,  isr_4_stub,  IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(5,  isr_5_stub,  IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(6,  isr_6_stub,  IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(7,  isr_7_stub,  IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(8,  isr_8_stub,  IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(9,  isr_9_stub,  IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(10, isr_10_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(11, isr_11_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(12, isr_12_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(13, isr_13_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(14, isr_14_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(15, isr_15_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(16, isr_16_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(17, isr_17_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(18, isr_18_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(19, isr_19_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(20, isr_20_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(21, isr_21_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(22, isr_22_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(23, isr_23_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(24, isr_24_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(25, isr_25_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(26, isr_26_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(27, isr_27_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(28, isr_28_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(29, isr_29_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(30, isr_30_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(31, isr_31_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(32, isr_32_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(33, isr_33_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(34, isr_34_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(35, isr_35_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(36, isr_36_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(37, isr_37_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(38, isr_38_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(39, isr_39_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(40, isr_40_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(41, isr_41_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(42, isr_42_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(43, isr_43_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(44, isr_44_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(45, isr_45_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(46, isr_46_stub, IDT_FLAG_INTERRUPT, 0);
    idt_set_gate(47, isr_47_stub, IDT_FLAG_INTERRUPT, 0);

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint64_t)idt;
    __asm__ volatile("lidt %0" : : "m"(idt_ptr));
}
