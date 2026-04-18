/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>
#include <core/drivers.h>

/* UART 16550 (x86_64 COM1 at 0x3F8) */
#define COM1_BASE  0x3F8
#define COM1_DATA  (COM1_BASE + 0)
#define COM1_IER   (COM1_BASE + 1)
#define COM1_FCR   (COM1_BASE + 2)
#define COM1_LCR   (COM1_BASE + 3)
#define COM1_MCR   (COM1_BASE + 4)
#define COM1_LSR   (COM1_BASE + 5)

#define LSR_DR    0x01   /* data ready */
#define LSR_THRE  0x20   /* transmitter holding register empty */

void serial_init(uint32_t baud) {
    uint16_t divisor = (uint16_t)(115200 / baud);
    io_outb(COM1_IER, 0x00);           /* disable interrupts */
    io_outb(COM1_LCR, 0x80);           /* enable DLAB */
    io_outb(COM1_DATA, (uint8_t)(divisor & 0xFF));
    io_outb(COM1_IER,  (uint8_t)(divisor >> 8));
    io_outb(COM1_LCR, 0x03);           /* 8-N-1, clear DLAB */
    io_outb(COM1_FCR, 0xC7);           /* enable FIFO, clear, 14-byte threshold */
    io_outb(COM1_MCR, 0x0B);           /* DTR, RTS, aux output 2 */
}

void serial_write(char c) {
    while (!(io_inb(COM1_LSR) & LSR_THRE)) {}
    io_outb(COM1_DATA, (uint8_t)c);
}

char serial_read(void) {
    while (!(io_inb(COM1_LSR) & LSR_DR)) {}
    return (char)io_inb(COM1_DATA);
}
