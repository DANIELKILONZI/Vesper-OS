#ifndef PORT_IO_H
#define PORT_IO_H

#include <stdint.h>

/*
 * VESPER OS – Low-level x86 I/O port helpers
 *
 * Inline wrappers around the IN / OUT x86 instructions.
 * Shared by the PIC, keyboard, and any future hardware drivers.
 */

/* Write one byte to the given I/O port */
static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

/* Read one byte from the given I/O port */
static inline uint8_t inb(uint16_t port)
{
    uint8_t result;
    __asm__ volatile ("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

/* Write one 16-bit word to the given I/O port */
static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

/* Read one 16-bit word from the given I/O port */
static inline uint16_t inw(uint16_t port)
{
    uint16_t result;
    __asm__ volatile ("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

/* Write one 32-bit dword to the given I/O port */
static inline void outl(uint16_t port, uint32_t value)
{
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

/* Read one 32-bit dword from the given I/O port */
static inline uint32_t inl(uint16_t port)
{
    uint32_t result;
    __asm__ volatile ("inl %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

/*
 * io_wait – Issue a dummy write to port 0x80 (POST diagnostic port).
 *
 * Older hardware requires a brief delay between consecutive I/O operations
 * (e.g. PIC initialisation sequence).  A write to 0x80 takes ~1–4 µs on
 * most x86 systems and is the canonical no-op I/O delay.
 */
static inline void io_wait(void)
{
    outb(0x80, 0x00);
}

#endif /* PORT_IO_H */
