#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

/*
 * VESPER OS – PS/2 Mouse driver (IRQ12)
 *
 * Decodes standard 3-byte PS/2 packets and maintains an accumulated
 * cursor position clamped to VGA text-mode bounds (0..79 x 0..24).
 *
 * Buttons are reported as a bitmask:
 *   bit 0 = left   button
 *   bit 1 = right  button
 *   bit 2 = middle button
 */

typedef struct {
    int16_t  x;        /* accumulated X position, clamped to 0..79  */
    int16_t  y;        /* accumulated Y position, clamped to 0..24  */
    uint8_t  buttons;  /* button bitmask: bit0=left,1=right,2=middle */
    uint32_t packets;  /* total valid 3-byte packets received         */
} mouse_state_t;

/*
 * mouse_init – Enable the PS/2 auxiliary device, turn on IRQ12 routing in
 *              the PS/2 controller command byte, and register the IRQ12
 *              handler on the slave PIC.
 */
void mouse_init(void);

/*
 * mouse_get_state – Return a snapshot of the current mouse state.
 */
mouse_state_t mouse_get_state(void);

#endif /* MOUSE_H */
