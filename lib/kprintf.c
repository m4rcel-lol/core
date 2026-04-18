/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>
#include <core/drivers.h>

/* Forward declarations for output functions */
extern void serial_write(char c);
extern void vga_putchar(char c, uint8_t colour);

static void kputc(char c) {
    serial_write(c);
    vga_putchar(c, VGA_DEFAULT_COLOR);
}

static void kputs(const char *s) {
    while (*s) kputc(*s++);
}

static void kput_uint(uint64_t val, int base, int width, char pad) {
    char buf[24];
    int  i = 0;
    const char *digits = "0123456789abcdef";
    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val) {
            buf[i++] = digits[val % (uint64_t)base];
            val /= (uint64_t)base;
        }
    }
    /* Pad */
    while (i < width) buf[i++] = pad;
    /* Reverse and print */
    for (int j = i - 1; j >= 0; j--) kputc(buf[j]);
}

static void kput_int(int64_t val, int width, char pad) {
    if (val < 0) { kputc('-'); val = -val; width--; }
    kput_uint((uint64_t)val, 10, width, pad);
}

#include <stdarg.h>

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') { kputc(*fmt++); continue; }
        fmt++;
        int  width = 0;
        char pad   = ' ';
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        /* length modifiers */
        int   is_long = 0;
        int   is_size = 0;
        if (*fmt == 'l') { is_long = 1; fmt++; if (*fmt == 'l') { is_long = 2; fmt++; } }
        else if (*fmt == 'z') { is_size = 1; fmt++; }
        char spec = *fmt++;
        switch (spec) {
        case 'd': {
            int64_t v = (is_long == 2) ? va_arg(ap, int64_t) :
                        (is_long == 1) ? va_arg(ap, int64_t) :
                                         (int64_t)va_arg(ap, int);
            kput_int(v, width, pad);
            break;
        }
        case 'u': {
            uint64_t v = (is_long == 2) ? va_arg(ap, uint64_t) :
                         (is_size)       ? va_arg(ap, size_t) :
                                           (uint64_t)va_arg(ap, unsigned int);
            kput_uint(v, 10, width, pad);
            break;
        }
        case 'x': {
            uint64_t v = (is_long == 2) ? va_arg(ap, uint64_t) :
                         (is_size)       ? va_arg(ap, size_t) :
                                           (uint64_t)va_arg(ap, unsigned int);
            kput_uint(v, 16, width, pad);
            break;
        }
        case 'p': {
            uint64_t v = (uint64_t)va_arg(ap, void *);
            kputs("0x");
            kput_uint(v, 16, 16, '0');
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            kputs(s);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            kputc(c);
            break;
        }
        case 'z': {
            /* %zu handled via length modifier above; bare %z is not a valid specifier */
            kputc('%');
            kputc('z');
            break;
        }
        case '%':
            kputc('%');
            break;
        default:
            kputc('%');
            kputc(spec);
            break;
        }
        (void)is_size;
    }
    va_end(ap);
}
