#include "fd.h"
#include "string.h"

typedef struct {
    fs_file_t file;    /* open file handle    */
    uint32_t  pid;     /* owning process PID  */
    int       used;    /* 1 = slot occupied   */
} fd_entry_t;

static fd_entry_t fd_table[FD_MAX];

/* -------------------------------------------------------------------------
 * fd_init – zero the file descriptor table
 * ---------------------------------------------------------------------- */
void fd_init(void)
{
    memset(fd_table, 0, sizeof(fd_table));
}

/* -------------------------------------------------------------------------
 * fd_open – open a file and return its descriptor
 * ---------------------------------------------------------------------- */
int fd_open(const char *name, uint32_t pid)
{
    /* Find a free slot */
    int slot = -1;
    for (uint32_t i = 0; i < FD_MAX; i++) {
        if (!fd_table[i].used) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) {
        return FD_ERR_TABLE_FULL;
    }

    if (fs_open(&fd_table[slot].file, name) != 0) {
        return FD_ERR_NOT_FOUND;
    }

    fd_table[slot].pid  = pid;
    fd_table[slot].used = 1;
    return slot;
}

/* -------------------------------------------------------------------------
 * fd_read – read from an open descriptor
 * ---------------------------------------------------------------------- */
int fd_read(int fd, void *buf, uint32_t len, uint32_t pid)
{
    if (fd < 0 || (uint32_t)fd >= FD_MAX) {
        return FD_ERR_INVALID;
    }
    fd_entry_t *e = &fd_table[fd];
    if (!e->used || e->pid != pid) {
        return FD_ERR_INVALID;
    }
    uint32_t n = fs_read(&e->file, buf, len);
    return (int)n;
}

/* -------------------------------------------------------------------------
 * fd_close – release a file descriptor
 * ---------------------------------------------------------------------- */
int fd_close(int fd, uint32_t pid)
{
    if (fd < 0 || (uint32_t)fd >= FD_MAX) {
        return FD_ERR_INVALID;
    }
    fd_entry_t *e = &fd_table[fd];
    if (!e->used || e->pid != pid) {
        return FD_ERR_INVALID;
    }
    fs_close(&e->file);
    e->used = 0;
    e->pid  = 0;
    return 0;
}

void fd_close_all_for_pid(uint32_t pid)
{
    for (uint32_t i = 0; i < FD_MAX; i++) {
        if (fd_table[i].used && fd_table[i].pid == pid) {
            fs_close(&fd_table[i].file);
            fd_table[i].used = 0;
            fd_table[i].pid  = 0;
        }
    }
}
