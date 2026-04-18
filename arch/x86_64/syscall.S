/* CORE Kernel — (c) CORE Project, MIT License */
/* arch/x86_64/syscall.S — SYSCALL/SYSRET entry stub */

.text
.global syscall_entry
syscall_entry:
    /* On SYSCALL entry:
     *   rcx = user rip (saved by hardware)
     *   r11 = user rflags (saved by hardware)
     *   rax = syscall number
     *   rdi, rsi, rdx, r10, r8, r9 = arguments (Linux ABI)
     * We swap to kernel stack via swapgs / kernel GS (simplified here).
     */
    swapgs
    movq %rsp, %gs:8     /* save user rsp */
    movq %gs:0, %rsp     /* load kernel rsp */

    /* Build struct regs on stack */
    pushq %r11           /* rflags */
    pushq %rcx           /* rip */
    pushq %rsp           /* rsp — we push twice, adjust below */
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %rcx
    pushq %rbx
    pushq %rax

    movq %rsp, %rdi     /* arg0 = &regs */
    call syscall_dispatch

    /* Restore registers from struct regs (rax = return value) */
    movq (%rsp), %rax    /* rax = return value */
    addq $8, %rsp        /* skip rax */
    popq %rbx
    popq %rcx
    popq %rdx
    popq %rsi
    popq %rdi
    popq %rbp
    popq %r8
    popq %r9
    popq %r10
    popq %r11
    popq %r12
    popq %r13
    popq %r14
    popq %r15
    addq $8, %rsp        /* skip saved rsp */
    popq %rcx            /* user rip */
    popq %r11            /* user rflags */

    movq %gs:8, %rsp     /* restore user rsp */
    swapgs
    sysretq
.section .note.GNU-stack,"",@progbits
