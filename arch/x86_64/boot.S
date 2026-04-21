/* CORE Kernel — (c) CORE Project, MIT License */
/* arch/x86_64/boot.S — Multiboot2 header, early stack, long mode entry */

.set MULTIBOOT2_MAGIC,    0xE85250D6
.set MULTIBOOT2_ARCH_X86, 0
.set HEADER_LENGTH,       (multiboot2_header_end - multiboot2_header_start)
.set HEADER_CHECKSUM,     -(MULTIBOOT2_MAGIC + MULTIBOOT2_ARCH_X86 + HEADER_LENGTH)

.set KERNEL_STACK_SIZE,   16384
.set BOOT_GDT_CODE_SEL,   0x08
.set BOOT_GDT_DATA_SEL,   0x10

.section .multiboot, "a"
.align 8
multiboot2_header_start:
    .long MULTIBOOT2_MAGIC
    .long MULTIBOOT2_ARCH_X86
    .long HEADER_LENGTH
    .long HEADER_CHECKSUM
    /* framebuffer tag */
    .word 5
    .word 0
    .long 20
    .long 0
    .long 0
    .long 0
    .align 8
    /* end tag */
    .word 0
    .word 0
    .long 8
multiboot2_header_end:

.section .bss, "aw", @nobits
.align 16
stack_bottom:
    .skip KERNEL_STACK_SIZE
stack_top:

/* Early page tables for identity map + higher half */
.align 4096
early_pml4:
    .skip 4096
early_pdpt_low:
    .skip 4096
early_pdpt_high:
    .skip 4096
early_pd:
    .skip 4096

.section .rodata, "a"
.align 8
boot_gdt:
    .quad 0x0000000000000000
    .quad 0x00AF9A000000FFFF
    .quad 0x00AF92000000FFFF
boot_gdt_end:

boot_gdt_ptr:
    .word boot_gdt_end - boot_gdt - 1
    .long boot_gdt

.section .text.boot, "ax"
.global _start
.code32
_start:
    /* EBX = multiboot2 info pointer, EAX = 0x36d76289 */
    cli
    movl $stack_top, %esp
    pushl $0
    pushl %ebx         /* save MB2 info */
    pushl $0
    pushl %eax         /* save MB2 magic */

    /* Load a minimal long-mode compatible GDT before the far jump. */
    lgdt boot_gdt_ptr
    movw $BOOT_GDT_DATA_SEL, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    /* Set up early page tables for long mode */
    /* PML4[0] -> early_pdpt_low (identity map) */
    movl $early_pdpt_low, %eax
    orl  $0x3, %eax
    movl %eax, early_pml4

    /* PML4[511] -> early_pdpt_high (higher half -2GB) */
    movl $early_pdpt_high, %eax
    orl  $0x3, %eax
    movl %eax, (early_pml4 + 511*8)

    /* early_pdpt_low[0] -> early_pd */
    movl $early_pd, %eax
    orl  $0x3, %eax
    movl %eax, early_pdpt_low

    /* early_pdpt_high[510] -> early_pd (kernel at 0xFFFFFFFF80000000) */
    movl $early_pd, %eax
    orl  $0x3, %eax
    movl %eax, (early_pdpt_high + 510*8)

    /* early_pd: map first 8 2MB pages (16MB) */
    movl $0, %ecx
    movl $early_pd, %edx
1:
    movl %ecx, %eax
    shll $21, %eax
    orl  $0x83, %eax   /* present + write + huge */
    movl %eax, (%edx)
    addl $8, %edx
    incl %ecx
    cmpl $8, %ecx
    jl   1b

    /* Load CR3 */
    movl $early_pml4, %eax
    movl %eax, %cr3

    /* Enable PAE */
    movl %cr4, %eax
    orl  $(1 << 5), %eax
    movl %eax, %cr4

    /* Enable long mode in EFER */
    movl $0xC0000080, %ecx
    rdmsr
    orl  $(1 << 8), %eax
    wrmsr

    /* Enable paging + protection */
    movl %cr0, %eax
    orl  $(1 << 31 | 1), %eax
    movl %eax, %cr0

    /* Far jump to 64-bit code */
    ljmpl $BOOT_GDT_CODE_SEL, $start64

.code64
start64:
    movw $BOOT_GDT_DATA_SEL, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs

    /* Switch to higher-half stack */
    movabsq $0xFFFFFFFF80000000, %rax
    addq %rax, %rsp

    /* Pop arguments saved earlier */
    popq %rdi   /* MB2 magic */
    popq %rsi   /* MB2 info pointer */

    /* Call kmain(uint32_t magic, uint64_t mb2_info) */
    movabsq $kmain, %rax
    callq *%rax

halt:
    hlt
    jmp halt

.section .data
.global __core_version
__core_version:
    .asciz "CORE-0.1.0"

.section .note.GNU-stack,"",@progbits
