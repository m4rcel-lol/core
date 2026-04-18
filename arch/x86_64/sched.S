/* CORE Kernel — (c) CORE Project, MIT License */
/* arch/x86_64/sched.S — context switch: save/restore GP registers */

.text
.global switch_context
/* void switch_context(struct proc *from, struct proc *to)
 * struct regs layout (from proc.h):
 *   rax=0, rbx=8, rcx=16, rdx=24, rsi=32, rdi=40, rbp=48,
 *   r8=56, r9=64, r10=72, r11=80, r12=88, r13=96, r14=104, r15=112,
 *   rsp=120, rip=128, rflags=136
 * ctx is at offset = sizeof(pid)*2 + sizeof(state) + padding
 * We'll use fixed offsets: ctx starts at byte 12 in struct proc
 * but with alignment it's at 16 (after pid=4, ppid=4, state=4, pad=4)
 */
#define CTX_OFF   16   /* offset of struct regs within struct proc */
#define RAX_OFF   (CTX_OFF + 0)
#define RBX_OFF   (CTX_OFF + 8)
#define RCX_OFF   (CTX_OFF + 16)
#define RDX_OFF   (CTX_OFF + 24)
#define RSI_OFF   (CTX_OFF + 32)
#define RDI_OFF   (CTX_OFF + 40)
#define RBP_OFF   (CTX_OFF + 48)
#define R8_OFF    (CTX_OFF + 56)
#define R9_OFF    (CTX_OFF + 64)
#define R10_OFF   (CTX_OFF + 72)
#define R11_OFF   (CTX_OFF + 80)
#define R12_OFF   (CTX_OFF + 88)
#define R13_OFF   (CTX_OFF + 96)
#define R14_OFF   (CTX_OFF + 104)
#define R15_OFF   (CTX_OFF + 112)
#define RSP_OFF   (CTX_OFF + 120)
#define RIP_OFF   (CTX_OFF + 128)
#define RFLAGS_OFF (CTX_OFF + 136)

switch_context:
    /* rdi = from, rsi = to */
    testq %rdi, %rdi
    jz    .load_ctx

    /* Save GP registers into from->ctx */
    movq %rax, RAX_OFF(%rdi)
    movq %rbx, RBX_OFF(%rdi)
    movq %rcx, RCX_OFF(%rdi)
    movq %rdx, RDX_OFF(%rdi)
    movq %rsi, RSI_OFF(%rdi)
    /* rdi saved below */
    movq %rbp, RBP_OFF(%rdi)
    movq %r8,  R8_OFF(%rdi)
    movq %r9,  R9_OFF(%rdi)
    movq %r10, R10_OFF(%rdi)
    movq %r11, R11_OFF(%rdi)
    movq %r12, R12_OFF(%rdi)
    movq %r13, R13_OFF(%rdi)
    movq %r14, R14_OFF(%rdi)
    movq %r15, R15_OFF(%rdi)
    movq %rsp, RSP_OFF(%rdi)

    /* Save rflags */
    pushfq
    popq %rax
    movq %rax, RFLAGS_OFF(%rdi)

    /* Save rip (return address from call) */
    movq (%rsp), %rax
    movq %rax, RIP_OFF(%rdi)

    /* Save original rdi */
    movq %rdi, RDI_OFF(%rdi)

.load_ctx:
    /* rsi = to */
    movq RIP_OFF(%rsi),    %rax
    movq RFLAGS_OFF(%rsi), %rbx

    movq RBP_OFF(%rsi),  %rbp
    movq R8_OFF(%rsi),   %r8
    movq R9_OFF(%rsi),   %r9
    movq R10_OFF(%rsi),  %r10
    movq R11_OFF(%rsi),  %r11
    movq R12_OFF(%rsi),  %r12
    movq R13_OFF(%rsi),  %r13
    movq R14_OFF(%rsi),  %r14
    movq R15_OFF(%rsi),  %r15

    /* Switch page tables if pml4 != 0 */
    movq 8(%rsi), %rcx     /* pml4 is at offset 8 relative to... */
    /* Actually pml4 offset: pid(4)+ppid(4)+state(4)+pad(4)+regs(144) = 160
       Let's skip CR3 switch here; it's done in sched.c */

    movq RSP_OFF(%rsi),  %rsp

    /* Restore rflags */
    pushq %rbx
    popfq

    movq RCX_OFF(%rsi),  %rcx
    movq RDX_OFF(%rsi),  %rdx
    movq RBX_OFF(%rsi),  %rbx
    movq RDI_OFF(%rsi),  %rdi
    movq RSI_OFF(%rsi),  %rsi

    /* Jump to saved rip */
    jmpq *%rax
.section .note.GNU-stack,"",@progbits
