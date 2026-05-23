#ifndef VESPER_H
#define VESPER_H

/*
 * Vesper OS – User-space C library header
 *
 * Provides thin wrappers around INT 0x80 syscalls so that user programs
 * can be written in standard C without any inline assembly.
 *
 * Syscall numbers (must match kernel/syscall.h):
 *   0  exit         4  yield        8  close       12  pipe_read
 *   1  write        5  kill         9  gettime
 *   2  getpid       6  open        10  pipe_create
 *   3  sleep        7  read        11  pipe_write
 */

typedef unsigned int   uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char  uint8_t;
typedef int            int32_t;

/* Null pointer */
#define NULL ((void *)0)

/* -------------------------------------------------------------------------
 * System call wrappers
 * ---------------------------------------------------------------------- */

/* Terminate the calling process */
void exit(int code) __attribute__((noreturn));

/* Write @len bytes from @buf to the VGA console. Returns bytes written. */
int write(const void *buf, uint32_t len);

/* Return the current process PID */
uint32_t getpid(void);

/* Sleep for @ms milliseconds */
void sleep(uint32_t ms);

/* Yield the CPU voluntarily */
void yield(void);

/* Kill process @pid. Returns 0 on success, -1 on failure. */
int kill(uint32_t pid);

/* Open a file by name. Returns fd >= 0, or -1 on error. */
int open(const char *name);

/* Read up to @len bytes from @fd into @buf. Returns bytes read or -1. */
int read(int fd, void *buf, uint32_t len);

/* Close file descriptor @fd */
void close(int fd);

/* Return seconds since midnight (RTC) */
uint32_t gettime(void);

/* Create an IPC pipe. Returns pipe_id >= 0, or -1 on error. */
int pipe_create(void);

/* Write @len bytes from @data to pipe @id. Returns bytes written or -1. */
int pipe_write(int id, const void *data, uint32_t len);

/* Read up to @len bytes from pipe @id into @buf. Returns bytes read or -1. */
int pipe_read(int id, void *buf, uint32_t len);

/* -------------------------------------------------------------------------
 * String / utility functions (no libc available)
 * ---------------------------------------------------------------------- */

/* Print a NUL-terminated string to the console */
void puts(const char *s);

/* Print an unsigned integer in decimal */
void print_uint(uint32_t n);

/* Print an unsigned integer in hexadecimal with 0x prefix */
void print_hex(uint32_t n);

/* Return length of a NUL-terminated string */
uint32_t strlen(const char *s);

#endif /* VESPER_H */
