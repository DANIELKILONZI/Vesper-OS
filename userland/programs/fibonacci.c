/*
 * fibonacci.c – Vesper OS user-space program
 *
 * Computes and prints the first 20 Fibonacci numbers.
 */
#include "../libc/vesper.h"

int main(void)
{
    puts("=== Fibonacci Sequence (first 20) ===\n");

    uint32_t a = 0, b = 1;

    for (int i = 0; i < 20; i++) {
        puts("  F(");
        print_uint((uint32_t)i);
        puts(") = ");
        print_uint(a);
        puts("\n");

        uint32_t tmp = a + b;
        a = b;
        b = tmp;
    }

    puts("Done!\n");
    return 0;
}
