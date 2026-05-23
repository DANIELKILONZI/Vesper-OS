/*
 * hello.c – Vesper OS user-space program
 *
 * Prints "Hello from user space!" and shows the process PID.
 */
#include "../libc/vesper.h"

int main(void)
{
    puts("=== Hello from Vesper OS user space! ===\n");
    puts("  PID: ");
    print_uint(getpid());
    puts("\n");
    puts("  Time (seconds since midnight): ");
    print_uint(gettime());
    puts("\n");
    puts("  Sleeping 500ms...\n");
    sleep(500);
    puts("  Awake! Goodbye.\n");
    return 0;
}
