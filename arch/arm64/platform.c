/* CORE Kernel — (c) CORE Project, MIT License */
/*
 * arch/arm64/platform.c — ARM64 platform implementations.
 *
 * Provides:
 *  - PL011 UART (QEMU virt machine serial console)
 *  - ARM64 Generic Timer (10 ms periodic tick)
 *  - VMM stubs (identity-mapped bring-up, no MMU enabled)
 *  - No-op stubs for x86_64-only subsystems (GDT, IDT, PIC, VGA, PS/2,
 *    SYSCALL MSR) that are compiled out of the ARM64 build via the Makefile
 *    but whose symbols are referenced from arch-independent kernel code.
 */

#include <core/types.h>
#include <core/mm.h>
#include <core/drivers.h>
#include <core/proc.h>
#include <core/syscall.h>

/* =========================================================================
 * PL011 UART — QEMU virt machine base address 0x09000000
 * ========================================================================= */

#define PL011_BASE      0x09000000ULL
#define PL011_UARTDR    ((volatile uint32_t *)(PL011_BASE + 0x000U))
#define PL011_UARTFR    ((volatile uint32_t *)(PL011_BASE + 0x018U))
#define PL011_UARTCR    ((volatile uint32_t *)(PL011_BASE + 0x030U))

#define PL011_FR_TXFF   (1U << 5)   /* TX FIFO full   */
#define PL011_FR_RXFE   (1U << 4)   /* RX FIFO empty  */
#define PL011_CR_RXE    (1U << 9)
#define PL011_CR_TXE    (1U << 8)
#define PL011_CR_UARTEN (1U << 0)

void serial_init(uint32_t baud) {
    (void)baud;
    /* QEMU PL011 is pre-configured by the machine model.
     * Ensure TX and RX paths are enabled. */
    *PL011_UARTCR = PL011_CR_RXE | PL011_CR_TXE | PL011_CR_UARTEN;
}

void serial_write(char c) {
    while (*PL011_UARTFR & PL011_FR_TXFF) {}
    *PL011_UARTDR = (uint32_t)(uint8_t)c;
}

char serial_read(void) {
    while (*PL011_UARTFR & PL011_FR_RXFE) {}
    return (char)(*PL011_UARTDR & 0xFFU);
}

/* =========================================================================
 * VGA stubs — no VGA framebuffer on ARM64 QEMU virt
 * ========================================================================= */

void vga_init(void)                        {}
void vga_putchar(char c, uint8_t colour)   { (void)c; (void)colour; }
void vga_scroll(void)                      {}
void vga_set_cursor(int row, int col)      { (void)row; (void)col; }

/* =========================================================================
 * GDT / IDT / PIC stubs
 * ARM64 has no x86 segment descriptor table or 8259A interrupt controller.
 * ========================================================================= */

void gdt_init(void) {}
void idt_init(void) {}
void pic_init(void) {}

/* =========================================================================
 * ARM64 Generic Timer (EL1 physical timer)
 * ========================================================================= */

void timer_init(void) {
    uint64_t freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    /* Programme a 10 ms interval and enable the physical countdown timer */
    uint64_t tval = freq / 100U;
    __asm__ volatile("msr cntp_tval_el0, %0" : : "r"(tval));
    __asm__ volatile("msr cntp_ctl_el0,  %0" : : "r"((uint64_t)1U));
}

uint64_t kernel_uptime_ms(void) {
    uint64_t cnt, freq;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(cnt));
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    return (freq > 0U) ? (cnt / (freq / 1000U)) : 0U;
}

void timer_sleep_ms(uint32_t ms) {
    uint64_t end = kernel_uptime_ms() + (uint64_t)ms;
    while (kernel_uptime_ms() < end)
        __asm__ volatile("wfe");
}

/* =========================================================================
 * PS/2 Keyboard stubs — no PS/2 controller on ARM64 QEMU virt
 * ========================================================================= */

void keyboard_init(void)                    {}
char keyboard_getchar(void)                 { return 0; }
int  keyboard_buf_read(void *buf, size_t n) { (void)buf; (void)n; return 0; }

/* =========================================================================
 * VMM stubs — identity-mapped bring-up; MMU not enabled
 *
 * All vmm_map / vmm_unmap calls are accepted but ignored.  vmm_fork
 * returns the same page-table pointer (shallow, same address space).
 * vmm_get_kernel_pml4 returns NULL; callers in elf.c guard for this.
 * ========================================================================= */

void vmm_init(void) {}   /* ARM64 MMU bring-up not required for boot */

int vmm_map(uint64_t *pml4, uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    (void)pml4; (void)vaddr; (void)paddr; (void)flags;
    return 0;
}

void vmm_unmap(uint64_t *pml4, uint64_t vaddr) {
    (void)pml4; (void)vaddr;
}

uint64_t *vmm_fork(uint64_t *pml4) {
    return pml4;
}

uint64_t *vmm_get_kernel_pml4(void) {
    return NULL;
}

int vmm_handle_cow(uint64_t addr, uint64_t err_code) {
    (void)addr; (void)err_code;
    return -1;
}

/* =========================================================================
 * Syscall stub — ARM64 uses SVC; the full dispatch table is not wired yet.
 * ========================================================================= */

void     syscall_init(void)              {}
uint64_t syscall_dispatch(struct regs *r)
{
    (void)r;
    return (uint64_t)(int64_t)-38;   /* -ENOSYS */
}
void     syscall_entry(void)             {}
