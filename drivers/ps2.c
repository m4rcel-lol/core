/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>
#include <core/drivers.h>
#include <core/signal.h>
#include <core/proc.h>

#define PS2_DATA_PORT  0x60
#define PS2_STATUS_PORT 0x64

/* 64-byte ring buffer */
#define KB_BUF_SIZE 64
static volatile char kb_buf[KB_BUF_SIZE];
static volatile size_t kb_read_pos  = 0;
static volatile size_t kb_write_pos = 0;
static volatile size_t kb_count     = 0;

static int shift_pressed   = 0;
static int caps_lock_on    = 0;
static int ctrl_pressed    = 0;
static int extended        = 0;

/* Scancode set 1 → ASCII (unshifted) */
static const char keymap_normal[128] = {
    0,    0,   '1', '2', '3', '4', '5', '6',
    '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    'o', 'p', '[', ']', '\n', 0,  'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'','`',  0,  '\\','z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0,   '*',
    0,   ' ', 0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   '7',
    '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
};

/* Scancode set 1 → ASCII (shifted) */
static const char keymap_shift[128] = {
    0,    0,   '!', '@', '#', '$', '%', '^',
    '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O', 'P', '{', '}', '\n', 0,  'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~',  0,  '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0,   '*',
    0,   ' ', 0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   '7',
    '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
};

#define SC_LSHIFT 0x2A
#define SC_RSHIFT 0x36
#define SC_CTRL   0x1D
#define SC_CAPS   0x3A
#define SC_EXTEND 0xE0

static void kb_buf_push(char c) {
    if (kb_count < KB_BUF_SIZE) {
        kb_buf[kb_write_pos] = c;
        kb_write_pos = (kb_write_pos + 1) % KB_BUF_SIZE;
        kb_count++;
    }
}

void keyboard_irq_handler(void) {
    uint8_t sc = io_inb(PS2_DATA_PORT);

    if (sc == SC_EXTEND) { extended = 1; return; }

    int released = (sc & 0x80) != 0;
    sc &= 0x7F;

    if (extended) { extended = 0; return; }

    if (sc == SC_LSHIFT || sc == SC_RSHIFT) {
        shift_pressed = !released;
        return;
    }
    if (sc == SC_CTRL) {
        ctrl_pressed = !released;
        return;
    }
    if (sc == SC_CAPS && !released) {
        caps_lock_on = !caps_lock_on;
        return;
    }

    if (released) return;

    char c;
    if (shift_pressed) {
        c = keymap_shift[sc];
    } else {
        c = keymap_normal[sc];
    }
    if (!c) return;

    /* Apply caps lock to letters */
    if (caps_lock_on && c >= 'a' && c <= 'z') c = (char)(c - 32);
    else if (caps_lock_on && c >= 'A' && c <= 'Z') c = (char)(c + 32);

    if (ctrl_pressed) {
        if (c == 'c' || c == 'C') {
            /* SIGINT to foreground process */
            extern struct proc *current;
            if (current) signal_send(current->pid, SIGINT);
            return;
        }
        return;
    }

    kb_buf_push(c);
}

char keyboard_getchar(void) {
    while (kb_count == 0) {
        __asm__ volatile("pause");
    }
    char c = kb_buf[kb_read_pos];
    kb_read_pos = (kb_read_pos + 1) % KB_BUF_SIZE;
    kb_count--;
    return c;
}

int keyboard_buf_read(void *buf, size_t n) {
    char *out = (char *)buf;
    size_t i = 0;
    while (i < n && kb_count > 0) {
        out[i++] = kb_buf[kb_read_pos];
        kb_read_pos = (kb_read_pos + 1) % KB_BUF_SIZE;
        kb_count--;
    }
    return (int)i;
}

void keyboard_init(void) {
    kb_read_pos  = 0;
    kb_write_pos = 0;
    kb_count     = 0;
    shift_pressed = 0;
    caps_lock_on  = 0;
    ctrl_pressed  = 0;
    extended      = 0;
    /* IRQ1 is already unmasked in pic_init() */
}
