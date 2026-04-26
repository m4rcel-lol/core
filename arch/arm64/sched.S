/* CORE Kernel — (c) CORE Project, MIT License */
/* arch/arm64/sched.S — AArch64 context switch */

/*
 * switch_context(struct proc *from, struct proc *to)
 *   x0 = from,  x1 = to
 *
 * struct regs layout (include/core/proc.h) — fields reused for AArch64:
 *   The struct has 18 × uint64_t entries starting at byte 0 within
 *   struct regs.  struct regs is embedded in struct proc at byte 16
 *   (after pid[4] + ppid[4] + state[4] + pad[4]).
 *
 *   We save/restore only the AArch64 callee-saved registers, mapping
 *   them onto the struct regs fields by fixed byte offset:
 *
 *     Field  Offset-in-regs  AArch64 register
 *     rax    0               x19
 *     rbx    8               x20
 *     rcx    16              x21
 *     rdx    24              x22
 *     rsi    32              x23
 *     rdi    40              x24
 *     rbp    48              x25
 *     r8     56              x26
 *     r9     64              x27
 *     r10    72              x28
 *     r11    80              x29 (frame pointer)
 *     rsp    120             sp
 *     rip    128             x30 (link register / return address)
 *
 *   Fields r12–r15 (88–119) and rflags (136) are not used on AArch64.
 *   rdi/rsi also seed x0/x1 when entering a fresh kernel thread.
 */

.text
.global switch_context

#define CTX_OFF   16      /* offset of struct regs inside struct proc */

#define X19_OFF   (CTX_OFF +   0)
#define X20_OFF   (CTX_OFF +   8)
#define X21_OFF   (CTX_OFF +  16)
#define X22_OFF   (CTX_OFF +  24)
#define X23_OFF   (CTX_OFF +  32)
#define X24_OFF   (CTX_OFF +  40)
#define X25_OFF   (CTX_OFF +  48)
#define X26_OFF   (CTX_OFF +  56)
#define X27_OFF   (CTX_OFF +  64)
#define X28_OFF   (CTX_OFF +  72)
#define X29_OFF   (CTX_OFF +  80)  /* frame pointer */
#define SP_OFF    (CTX_OFF + 120)  /* rsp field     */
#define LR_OFF    (CTX_OFF + 128)  /* rip field     */

switch_context:
    /* If from == NULL, skip saving (first context switch) */
    cbz  x0, .load_ctx

    /* Save AArch64 callee-saved registers into from->ctx */
    str  x19, [x0, #X19_OFF]
    str  x20, [x0, #X20_OFF]
    str  x21, [x0, #X21_OFF]
    str  x22, [x0, #X22_OFF]
    str  x23, [x0, #X23_OFF]
    str  x24, [x0, #X24_OFF]
    str  x25, [x0, #X25_OFF]
    str  x26, [x0, #X26_OFF]
    str  x27, [x0, #X27_OFF]
    str  x28, [x0, #X28_OFF]
    str  x29, [x0, #X29_OFF]   /* frame pointer */

    mov  x2, sp
    str  x2,  [x0, #SP_OFF]    /* stack pointer */
    str  x30, [x0, #LR_OFF]    /* link register (return address) */

.load_ctx:
    /* Restore AArch64 callee-saved registers from to->ctx */
    ldr  x19, [x1, #X19_OFF]
    ldr  x20, [x1, #X20_OFF]
    ldr  x21, [x1, #X21_OFF]
    ldr  x22, [x1, #X22_OFF]
    ldr  x23, [x1, #X23_OFF]
    ldr  x24, [x1, #X24_OFF]
    ldr  x25, [x1, #X25_OFF]
    ldr  x26, [x1, #X26_OFF]
    ldr  x27, [x1, #X27_OFF]
    ldr  x28, [x1, #X28_OFF]
    ldr  x29, [x1, #X29_OFF]   /* frame pointer */

    ldr  x2,  [x1, #SP_OFF]
    mov  sp,  x2               /* stack pointer */
    ldr  x30, [x1, #LR_OFF]   /* link register  */

    ldr  x0,  [x1, #X24_OFF]  /* first argument  */
    ldr  x2,  [x1, #X23_OFF]  /* second argument */
    mov  x1,  x2

    ret                        /* branch to x30 */
