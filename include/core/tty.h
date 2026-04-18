/* CORE Kernel — (c) CORE Project, MIT License */
#ifndef CORE_TTY_H
#define CORE_TTY_H

#include <core/types.h>

/* ───── c_cc[] slot indices (POSIX) ─────────────────────────────────────── */
#define VINTR     0
#define VQUIT     1
#define VERASE    2
#define VKILL     3
#define VEOF      4
#define VTIME     5
#define VMIN      6
#define VSWTC     7
#define VSTART    8
#define VSTOP     9
#define VSUSP    10
#define VEOL     11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE  14
#define VLNEXT   15
#define VEOL2    16
#define NCCS     19

/* ───── c_iflag bits ──────────────────────────────────────────────────────── */
#define IGNBRK  0000001U   /* ignore BREAK condition */
#define BRKINT  0000002U   /* signal interrupt on BREAK */
#define IGNPAR  0000004U   /* ignore chars with framing/parity errors */
#define PARMRK  0000010U   /* mark parity/framing errors */
#define INPCK   0000020U   /* enable input parity checking */
#define ISTRIP  0000040U   /* strip 8th bit */
#define INLCR   0000100U   /* map NL to CR on input */
#define IGNCR   0000200U   /* ignore CR */
#define ICRNL   0000400U   /* map CR to NL on input */
#define IUCLC   0001000U   /* map uppercase to lowercase on input */
#define IXON    0002000U   /* enable XON/XOFF flow control on output */
#define IXANY   0004000U   /* any char restarts stopped output */
#define IXOFF   0010000U   /* enable XON/XOFF flow control on input */
#define IMAXBEL 0020000U   /* ring bell when input queue is full */
#define IUTF8   0040000U   /* input is UTF-8 */

/* ───── c_oflag bits ──────────────────────────────────────────────────────── */
#define OPOST   0000001U   /* enable output processing */
#define OLCUC   0000002U   /* map lowercase to uppercase on output */
#define ONLCR   0000004U   /* map NL to CR-NL on output */
#define OCRNL   0000010U   /* map CR to NL on output */
#define ONOCR   0000020U   /* no CR output at column 0 */
#define ONLRET  0000040U   /* NL performs the CR function */
#define OFILL   0000100U   /* use fill chars for delay */
#define OFDEL   0000200U   /* fill char is DEL */

/* ───── c_cflag bits ──────────────────────────────────────────────────────── */
#define CSIZE   0000060U   /* character size mask */
#define   CS5   0000000U   /* 5-bit chars */
#define   CS6   0000020U   /* 6-bit chars */
#define   CS7   0000040U   /* 7-bit chars */
#define   CS8   0000060U   /* 8-bit chars */
#define CSTOPB  0000100U   /* 2 stop bits */
#define CREAD   0000200U   /* enable receiver */
#define PARENB  0000400U   /* enable parity */
#define PARODD  0001000U   /* odd parity */
#define HUPCL   0002000U   /* hang up on last close */
#define CLOCAL  0004000U   /* ignore modem status lines */

/* Baud rate encodings (Linux values embedded in c_cflag) */
#define B0      0000000U
#define B50     0000001U
#define B75     0000002U
#define B110    0000003U
#define B134    0000004U
#define B150    0000005U
#define B200    0000006U
#define B300    0000007U
#define B600    0000010U
#define B1200   0000011U
#define B1800   0000012U
#define B2400   0000013U
#define B4800   0000014U
#define B9600   0000015U
#define B19200  0000016U
#define B38400  0000017U
#define BOTHER  0010000U
#define B57600  0010001U
#define B115200 0010002U

/* ───── c_lflag bits ──────────────────────────────────────────────────────── */
#define ISIG    0000001U   /* generate signal on special chars */
#define ICANON  0000002U   /* canonical (line-based) mode */
#define XCASE   0000004U   /* canonical upper/lower presentation */
#define ECHO    0000010U   /* enable echo */
#define ECHOE   0000020U   /* echo erase char as BS-SP-BS */
#define ECHOK   0000040U   /* echo NL after kill char */
#define ECHONL  0000100U   /* echo NL even if ECHO is off */
#define NOFLSH  0000200U   /* do not flush after interrupt/quit */
#define TOSTOP  0000400U   /* SIGTTOU for background writes */
#define IEXTEN  0100000U   /* enable extended processing */

/* ───── struct termios ────────────────────────────────────────────────────── */
struct termios {
    uint32_t c_iflag;      /* input mode flags */
    uint32_t c_oflag;      /* output mode flags */
    uint32_t c_cflag;      /* control mode flags */
    uint32_t c_lflag;      /* local mode flags */
    uint8_t  c_line;       /* line discipline */
    uint8_t  c_cc[NCCS];   /* control characters */
};

/* ───── struct winsize ────────────────────────────────────────────────────── */
struct winsize {
    uint16_t ws_row;       /* rows, in characters */
    uint16_t ws_col;       /* columns, in characters */
    uint16_t ws_xpixel;    /* horizontal size, in pixels */
    uint16_t ws_ypixel;    /* vertical size, in pixels */
};

/* ───── ioctl(2) request codes (Linux/x86_64 values) ─────────────────────── */
#define TCGETS      0x5401UL   /* tcgetattr */
#define TCSETS      0x5402UL   /* tcsetattr (TCSANOW) */
#define TCSETSW     0x5403UL   /* tcsetattr (TCSADRAIN) */
#define TCSETSF     0x5404UL   /* tcsetattr (TCSAFLUSH) */
#define TCFLSH      0x540BUL   /* tcflush */
#define TIOCGPGRP   0x540FUL   /* get foreground process group */
#define TIOCSPGRP   0x5410UL   /* set foreground process group */
#define FIONREAD    0x541BUL   /* get number of bytes available to read */
#define TIOCGWINSZ  0x5413UL   /* get window size */
#define TIOCSWINSZ  0x5414UL   /* set window size */

/* ───── Sane cooked-mode defaults returned by TCGETS ─────────────────────── */
#define CORE_TERM_IFLAG  (ICRNL | IXON)
#define CORE_TERM_OFLAG  (OPOST | ONLCR)
#define CORE_TERM_CFLAG  (CS8 | CREAD | CLOCAL | B38400)
#define CORE_TERM_LFLAG  (ICANON | ECHO | ECHOE | ECHOK | ISIG | IEXTEN)

#endif /* CORE_TTY_H */
