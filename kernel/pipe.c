#include "pipe.h"
#include "process.h"
#include "string.h"

typedef struct {
    uint8_t  buf[PIPE_BUF_SIZE];  /* ring buffer                   */
    uint32_t head;                /* read index                    */
    uint32_t tail;                /* write index                   */
    uint32_t count;               /* bytes currently in the buffer */
    int      open;                /* 1 = allocated                 */
} pipe_t;

static pipe_t pipes[MAX_PIPES];

/* -------------------------------------------------------------------------
 * pipe_init – zero the pipe table
 * ---------------------------------------------------------------------- */
void pipe_init(void)
{
    memset(pipes, 0, sizeof(pipes));
}

/* -------------------------------------------------------------------------
 * pipe_alloc – find a free slot and mark it open
 * ---------------------------------------------------------------------- */
int pipe_alloc(void)
{
    for (uint32_t i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].open) {
            memset(&pipes[i], 0, sizeof(pipe_t));
            pipes[i].open = 1;
            return (int)i;
        }
    }
    return -1;   /* table full */
}

/* -------------------------------------------------------------------------
 * pipe_write – push data into the pipe ring buffer
 * ---------------------------------------------------------------------- */
int pipe_write(int id, const void *data, uint32_t len)
{
    if (id < 0 || (uint32_t)id >= MAX_PIPES || !pipes[id].open) {
        return -1;
    }

    pipe_t *p = &pipes[id];
    const uint8_t *src = (const uint8_t *)data;
    uint32_t written = 0;

    while (written < len && p->count < PIPE_BUF_SIZE) {
        p->buf[p->tail] = src[written++];
        p->tail = (p->tail + 1u) % PIPE_BUF_SIZE;
        p->count++;
    }

    /* Wake any readers blocked on this pipe */
    if (written > 0) {
        process_wake_reason(WAIT_PIPE);
    }

    return (int)written;
}

/* -------------------------------------------------------------------------
 * pipe_read – consume data from the pipe, blocking if empty
 * ---------------------------------------------------------------------- */
int pipe_read(int id, void *buf, uint32_t len)
{
    if (id < 0 || (uint32_t)id >= MAX_PIPES || !pipes[id].open) {
        return -1;
    }

    pipe_t *p = &pipes[id];

    /* Block until data arrives or the pipe is closed */
    while (p->count == 0) {
        if (!p->open) {
            return -1;
        }
        if (!current_process) {
            return -1;   /* no scheduler: cannot block */
        }
        process_block_on(WAIT_PIPE);  /* targeted wakeup on pipe write */
    }

    /* Consume min(len, count) bytes */
    uint32_t to_read = (len < p->count) ? len : p->count;
    uint8_t *dst = (uint8_t *)buf;

    for (uint32_t i = 0; i < to_read; i++) {
        dst[i] = p->buf[p->head];
        p->head = (p->head + 1u) % PIPE_BUF_SIZE;
        p->count--;
    }

    return (int)to_read;
}

/* -------------------------------------------------------------------------
 * pipe_close – release the pipe slot and wake any blocked reader
 * ---------------------------------------------------------------------- */
void pipe_close(int id)
{
    if (id < 0 || (uint32_t)id >= MAX_PIPES) {
        return;
    }
    pipes[id].open = 0;
    /* Wake any process that might be blocked in pipe_read */
    process_wake_reason(WAIT_PIPE);
}
