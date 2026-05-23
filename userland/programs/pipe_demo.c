/*
 * pipe_demo.c – Vesper OS user-space program
 *
 * Demonstrates IPC pipes: creates a pipe, writes a message, reads it back.
 * (In a real scenario, writer and reader would be separate processes.)
 */
#include "../libc/vesper.h"

int main(void)
{
    puts("=== IPC Pipe Demo ===\n");

    int id = pipe_create();
    if (id < 0) {
        puts("ERROR: Failed to create pipe!\n");
        return 1;
    }

    puts("  Created pipe ID: ");
    print_uint((uint32_t)id);
    puts("\n");

    /* Write a message into the pipe */
    const char *msg = "Hello through the pipe!";
    uint32_t msg_len = strlen(msg);

    int written = pipe_write(id, msg, msg_len);
    puts("  Wrote ");
    print_uint((uint32_t)written);
    puts(" bytes\n");

    /* Read it back */
    char buf[64];
    int nread = pipe_read(id, buf, 63);
    if (nread > 0) {
        buf[nread] = '\0';
        puts("  Read back: \"");
        puts(buf);
        puts("\"\n");
    } else {
        puts("  ERROR: pipe_read failed!\n");
    }

    puts("Pipe demo complete.\n");
    return 0;
}
