/* CORE Kernel — (c) CORE Project, MIT License */
/* linker.ld — kernel link script for x86_64 */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH("i386:x86-64")
ENTRY(_start)

KERNEL_BASE = 0xFFFFFFFF80000000;
LOAD_BASE   = 0x100000;

SECTIONS {
    . = LOAD_BASE;

    .multiboot ALIGN(8) : {
        KEEP(*(.multiboot))
    }

    .text ALIGN(4096) : {
        *(.text.boot)
        *(.text*)
    }

    .rodata ALIGN(4096) : {
        *(.rodata*)
    }

    .data ALIGN(4096) : {
        *(.data*)
    }

    .bss ALIGN(4096) : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.eh_frame)
        *(.note*)
        *(.comment*)
    }
}
