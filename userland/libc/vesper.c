#include "vesper.h"

/*
 * Vesper OS – User-space C library implementation
 *
 * All syscalls use the INT 0x80 convention:
 *   EAX = syscall number
 *   EBX = arg0, ECX = arg1, EDX = arg2
 *   Return value in EAX
 */

/* -------------------------------------------------------------------------
 * Raw syscall helpers (inline assembly, i386 calling convention)
 * ---------------------------------------------------------------------- */

static inline uint32_t syscall0(uint32_t num)
{
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "memory"
    );
    return ret;
}

static inline uint32_t syscall1(uint32_t num, uint32_t arg0)
{
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg0)
        : "memory"
    );
    return ret;
}

static inline uint32_t syscall2(uint32_t num, uint32_t arg0, uint32_t arg1)
{
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg0), "c"(arg1)
        : "memory"
    );
    return ret;
}

static inline uint32_t syscall3(uint32_t num, uint32_t arg0, uint32_t arg1,
                                 uint32_t arg2)
{
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg0), "c"(arg1), "d"(arg2)
        : "memory"
    );
    return ret;
}

/* -------------------------------------------------------------------------
 * Syscall wrappers
 * ---------------------------------------------------------------------- */

#define SYS_EXIT         0u
#define SYS_WRITE        1u
#define SYS_GETPID       2u
#define SYS_SLEEP        3u
#define SYS_YIELD        4u
#define SYS_KILL         5u
#define SYS_OPEN         6u
#define SYS_READ         7u
#define SYS_CLOSE        8u
#define SYS_GETTIME      9u
#define SYS_PIPE_CREATE 10u
#define SYS_PIPE_WRITE  11u
#define SYS_PIPE_READ   12u

void exit(int code)
{
    (void)code;  /* Vesper doesn't use exit codes yet */
    syscall0(SYS_EXIT);
    /* Should never return, but satisfy the compiler */
    while (1) {}
}

int write(const void *buf, uint32_t len)
{
    return (int)syscall2(SYS_WRITE, (uint32_t)buf, len);
}

uint32_t getpid(void)
{
    return syscall0(SYS_GETPID);
}

void sleep(uint32_t ms)
{
    syscall1(SYS_SLEEP, ms);
}

void yield(void)
{
    syscall0(SYS_YIELD);
}

int kill(uint32_t pid)
{
    return (int)syscall1(SYS_KILL, pid);
}

int open(const char *name)
{
    return (int)syscall1(SYS_OPEN, (uint32_t)name);
}

int read(int fd, void *buf, uint32_t len)
{
    return (int)syscall3(SYS_READ, (uint32_t)fd, len, (uint32_t)buf);
}

void close(int fd)
{
    syscall1(SYS_CLOSE, (uint32_t)fd);
}

uint32_t gettime(void)
{
    return syscall0(SYS_GETTIME);
}

int pipe_create(void)
{
    return (int)syscall0(SYS_PIPE_CREATE);
}

int pipe_write(int id, const void *data, uint32_t len)
{
    return (int)syscall3(SYS_PIPE_WRITE, (uint32_t)id, len, (uint32_t)data);
}

int pipe_read(int id, void *buf, uint32_t len)
{
    return (int)syscall3(SYS_PIPE_READ, (uint32_t)id, len, (uint32_t)buf);
}

/* -------------------------------------------------------------------------
 * Utility functions
 * ---------------------------------------------------------------------- */

void puts(const char *s)
{
    uint32_t len = 0;
    while (s[len]) len++;
    write(s, len);
}

void print_uint(uint32_t n)
{
    char buf[12];
    int  i = 0;

    if (n == 0) {
        puts("0");
        return;
    }

    while (n > 0) {
        buf[i++] = '0' + (char)(n % 10);
        n /= 10;
    }

    /* Reverse and print */
    char out[12];
    for (int j = 0; j < i; j++) {
        out[j] = buf[i - 1 - j];
    }
    out[i] = '\0';
    puts(out);
}

void print_hex(uint32_t n)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0';
    buf[1] = 'x';

    for (int i = 7; i >= 0; i--) {
        buf[2 + (7 - i)] = hex[(n >> (i * 4)) & 0xF];
    }
    buf[10] = '\0';
    puts(buf);
}

uint32_t strlen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}
