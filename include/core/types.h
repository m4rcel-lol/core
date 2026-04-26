/* CORE Kernel — (c) CORE Project, MIT License */
#ifndef CORE_TYPES_H
#define CORE_TYPES_H

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

typedef uint64_t size_t;
typedef int64_t  ssize_t;
typedef int64_t  off_t;
typedef int32_t  pid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint32_t mode_t;
typedef uint64_t ino_t;
typedef uint64_t dev_t;
typedef uint64_t nlink_t;
typedef uint64_t blksize_t;
typedef uint64_t blkcnt_t;

typedef int      bool;
#define true  1
#define false 0

#define NULL ((void *)0)

#define UINT8_MAX  0xFFU
#define UINT16_MAX 0xFFFFU
#define UINT32_MAX 0xFFFFFFFFU
#define UINT64_MAX 0xFFFFFFFFFFFFFFFFULL

#define INT32_MIN  (-0x7FFFFFFF - 1)
#define INT32_MAX  0x7FFFFFFF

#define PAGE_SIZE  4096UL
#define PAGE_SHIFT 12

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ALIGN_UP(x, a)   (((x) + ((a) - 1)) & ~((a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#ifdef __aarch64__
static inline void io_outb(uint16_t port, uint8_t val) { (void)port; (void)val; }
static inline uint8_t io_inb(uint16_t port) { (void)port; return 0; }
static inline void io_outw(uint16_t port, uint16_t val) { (void)port; (void)val; }
static inline uint16_t io_inw(uint16_t port) { (void)port; return 0; }
static inline void io_wait(void) {}
#else
static inline void io_outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t io_inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void io_outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t io_inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void io_wait(void) {
    io_outb(0x80, 0);
}
#endif

#endif /* CORE_TYPES_H */
