/* CORE Kernel — (c) CORE Project, MIT License */
#ifndef CORE_DRIVERS_H
#define CORE_DRIVERS_H

#include <core/types.h>

/* serial.c */
void serial_init(uint32_t baud);
void serial_write(char c);
char serial_read(void);

/* vga.c */
void vga_init(void);
void vga_putchar(char c, uint8_t colour);
void vga_scroll(void);
void vga_set_cursor(int row, int col);

/* ps2.c */
void keyboard_init(void);
char keyboard_getchar(void);
int  keyboard_buf_read(void *buf, size_t n);

/* timer.c */
void     timer_init(void);
uint64_t kernel_uptime_ms(void);
void     timer_sleep_ms(uint32_t ms);

/* kprintf.c */
void kprintf(const char *fmt, ...);

/* VGA colour constants */
#define VGA_COLOR_BLACK        0
#define VGA_COLOR_BLUE         1
#define VGA_COLOR_GREEN        2
#define VGA_COLOR_CYAN         3
#define VGA_COLOR_RED          4
#define VGA_COLOR_MAGENTA      5
#define VGA_COLOR_BROWN        6
#define VGA_COLOR_LIGHT_GREY   7
#define VGA_COLOR_DARK_GREY    8
#define VGA_COLOR_LIGHT_BLUE   9
#define VGA_COLOR_LIGHT_GREEN  10
#define VGA_COLOR_LIGHT_CYAN   11
#define VGA_COLOR_LIGHT_RED    12
#define VGA_COLOR_LIGHT_MAGENTA 13
#define VGA_COLOR_LIGHT_BROWN  14
#define VGA_COLOR_WHITE        15

#define VGA_ENTRY_COLOR(fg, bg) ((uint8_t)((bg) << 4 | (fg)))
#define VGA_DEFAULT_COLOR VGA_ENTRY_COLOR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK)

#endif /* CORE_DRIVERS_H */
