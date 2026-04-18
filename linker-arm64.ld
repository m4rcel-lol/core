/* CORE Kernel — (c) CORE Project, MIT License */
/* linker-arm64.ld — kernel link script for AArch64 */

OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH("aarch64")
ENTRY(_start)

/*
 * QEMU virt machine: DRAM starts at 0x40000000.
 * -kernel loads the image at 0x40080000 by default.
 * The kernel runs identity-mapped (physical == virtual) during bring-up.
 */
KERNEL_LOAD = 0x40080000;

SECTIONS {
    . = KERNEL_LOAD;

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
        *(.multiboot)
    }
}
