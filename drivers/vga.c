/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>
#include <core/drivers.h>

#define VGA_WIDTH    80
#define VGA_HEIGHT   25
#define VGA_MEM      ((uint16_t *)0xB8000)

#define VGA_CTRL_REG 0x3D4
#define VGA_DATA_REG 0x3D5
#define VGA_CUR_HIGH 14
#define VGA_CUR_LOW  15

static int vga_col = 0;
static int vga_row = 0;
static uint8_t vga_color = VGA_DEFAULT_COLOR;

static uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)((uint16_t)color << 8) | (uint8_t)c;
}

void vga_set_cursor(int row, int col) {
    uint16_t pos = (uint16_t)(row * VGA_WIDTH + col);
    io_outb(VGA_CTRL_REG, VGA_CUR_HIGH);
    io_outb(VGA_DATA_REG, (uint8_t)(pos >> 8));
    io_outb(VGA_CTRL_REG, VGA_CUR_LOW);
    io_outb(VGA_DATA_REG, (uint8_t)(pos & 0xFF));
}

void vga_init(void) {
    for (int r = 0; r < VGA_HEIGHT; r++) {
        for (int c = 0; c < VGA_WIDTH; c++) {
            VGA_MEM[r * VGA_WIDTH + c] = vga_entry(' ', VGA_DEFAULT_COLOR);
        }
    }
    vga_row = 0;
    vga_col = 0;
    vga_set_cursor(0, 0);
}

void vga_scroll(void) {
    for (int r = 1; r < VGA_HEIGHT; r++) {
        for (int c = 0; c < VGA_WIDTH; c++) {
            VGA_MEM[(r - 1) * VGA_WIDTH + c] = VGA_MEM[r * VGA_WIDTH + c];
        }
    }
    for (int c = 0; c < VGA_WIDTH; c++) {
        VGA_MEM[(VGA_HEIGHT - 1) * VGA_WIDTH + c] = vga_entry(' ', vga_color);
    }
    vga_row = VGA_HEIGHT - 1;
}

void vga_putchar(char c, uint8_t color) {
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (c == '\r') {
        vga_col = 0;
    } else if (c == '\b') {
        if (vga_col > 0) {
            vga_col--;
            VGA_MEM[vga_row * VGA_WIDTH + vga_col] = vga_entry(' ', color);
        }
    } else if (c == '\t') {
        int spaces = 4 - (vga_col % 4);
        for (int i = 0; i < spaces; i++) {
            VGA_MEM[vga_row * VGA_WIDTH + vga_col] = vga_entry(' ', color);
            vga_col++;
            if (vga_col >= VGA_WIDTH) { vga_col = 0; vga_row++; break; }
        }
    } else {
        VGA_MEM[vga_row * VGA_WIDTH + vga_col] = vga_entry(c, color);
        vga_col++;
    }
    if (vga_col >= VGA_WIDTH) {
        vga_col = 0;
        vga_row++;
    }
    if (vga_row >= VGA_HEIGHT) {
        vga_scroll();
    }
    vga_set_cursor(vga_row, vga_col);
}
