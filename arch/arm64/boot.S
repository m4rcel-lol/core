/* CORE Kernel — (c) CORE Project, MIT License */
/* arch/arm64/boot.S — EL1 entry, MMU enable, early stack, call kmain() */

.section .text.boot
.global _start

_start:
    /* Disable all interrupts */
    msr daifset, #0xf

    /* Determine if we are at EL2, drop to EL1 if so */
    mrs x0, currentel
    lsr x0, x0, #2
    cmp x0, #2
    bne .at_el1

    /* Configure EL1 from EL2. HCR_EL2.RW (bit 31) selects AArch64 EL1. */
    movz x0, #0x8000, lsl #16
    msr hcr_el2, x0

    /* Allow EL1 access to the physical counter/timer used by platform.c. */
    mov x0, #0x3
    msr cnthctl_el2, x0
    msr cntvoff_el2, xzr

    mov x0, #0x3c5        /* SPSR_EL2: masked IRQs, return to EL1h */
    msr spsr_el2, x0
    adr x0, .at_el1
    msr elr_el2, x0
    eret

.at_el1:
    /* Disable EL1 alignment checking; code is also built with -mstrict-align. */
    mrs x0, sctlr_el1
    bic x0, x0, #(1 << 1)
    msr sctlr_el1, x0
    isb

    /* Set up early stack */
    adrp x0, stack_top
    add  x0, x0, :lo12:stack_top
    mov  sp, x0

    /* Clear BSS */
    adrp x1, __bss_start
    add  x1, x1, :lo12:__bss_start
    adrp x2, __bss_end
    add  x2, x2, :lo12:__bss_end
.bss_loop:
    cmp  x1, x2
    bge  .bss_done
    str  xzr, [x1], #8
    b    .bss_loop
.bss_done:

    /* Call kmain(0, 0) — no bootloader info on ARM64 simple path */
    mov  x0, #0
    mov  x1, #0
    bl   kmain

.halt:
    wfe
    b .halt

.section .bss
.align 4
.skip 16384
stack_top:

.section .data
.global __core_version
__core_version:
    .asciz "CORE-0.1.0"
.section .note.GNU-stack,"",@progbits
