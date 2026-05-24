#ifndef FD_H
#define FD_H

#include <stdint.h>
#include "fs.h"

/*
 * VESPER OS – Global file descriptor table  (Tier 3)
 *
 * The kernel maintains a flat table of FD_MAX open file handles.
 * Each slot records the owning process PID so that a process cannot
 * accidentally read/close another process's descriptor.
 *
 * A file descriptor (fd) is simply an index into this table (0–FD_MAX-1).
 *
 * Usage:
 *   int fd = fd_open("readme.txt", current_process->pid);
 *   fd_read(fd, buf, 128, current_process->pid);
 *   fd_close(fd, current_process->pid);
 */

#define FD_MAX   16u   /* max simultaneously open files system-wide */

#define FD_ERR_NOT_FOUND   (-1)
#define FD_ERR_TABLE_FULL  (-2)
#define FD_ERR_INVALID     (-3)

/* Initialise the file descriptor table (call once from kernel_main) */
void fd_init(void);

/*
 * fd_open – open a VesperFS file for reading and return a descriptor.
 * @name : NUL-terminated filename
 * @pid  : PID of the calling process
 * Returns fd (≥ 0) on success, -1 on error (not found, table full).
 */
int fd_open(const char *name, uint32_t pid);

/*
 * fd_read – read up to @len bytes from descriptor @fd into @buf.
 * @pid : must match the pid that opened this fd.
 * Returns bytes read, 0 at EOF, or -1 on error.
 */
int fd_read(int fd, void *buf, uint32_t len, uint32_t pid);

/*
 * fd_close – release a file descriptor.
 * @pid : must match the pid that opened this fd.
 */
int fd_close(int fd, uint32_t pid);

/* Close every descriptor owned by @pid. */
void fd_close_all_for_pid(uint32_t pid);

#endif /* FD_H */
