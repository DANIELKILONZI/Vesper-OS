#include "mouse.h"
#include "port_io.h"
#include "isr.h"
#include "pic.h"
#include "string.h"

/*
 * PS/2 controller I/O ports (shared with keyboard driver).
 * The DATA port is read/written for both keyboard and mouse bytes;
 * the CMD port is used to route commands to the auxiliary channel.
 */
#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

/* Busy-loop iteration cap – prevents infinite spin on broken hardware */
#define PS2_TIMEOUT 100000u

/* -------------------------------------------------------------------------
 * Low-level PS/2 helpers
 * ---------------------------------------------------------------------- */

/* Spin until the input buffer is empty (bit 1 clear = OK to write) */
static void ps2_wait_write(void)
{
    uint32_t t = PS2_TIMEOUT;
    while ((inb(PS2_STATUS) & 0x02u) && t--) {}
}

/* Spin until the output buffer has data (bit 0 set = byte ready) */
static void ps2_wait_read(void)
{
    uint32_t t = PS2_TIMEOUT;
    while (!(inb(PS2_STATUS) & 0x01u) && t--) {}
}

/* Send one byte to the PS/2 auxiliary (mouse) device */
static void mouse_write(uint8_t val)
{
    ps2_wait_write();
    outb(PS2_CMD,  0xD4);  /* 0xD4 = "next byte goes to auxiliary port" */
    ps2_wait_write();
    outb(PS2_DATA, val);
}

/* Read one byte from the PS/2 data port */
static uint8_t mouse_read(void)
{
    ps2_wait_read();
    return inb(PS2_DATA);
}

/* -------------------------------------------------------------------------
 * Interrupt-driven 3-byte packet accumulator
 * ---------------------------------------------------------------------- */
static volatile uint8_t      pkt[3];
static volatile uint8_t      pkt_idx = 0;
static volatile mouse_state_t ms;

static void mouse_irq_handler(registers_t *regs)
{
    (void)regs;

    uint8_t b = inb(PS2_DATA);

    /*
     * Sync on the first byte of each packet: bit 3 of the status byte is
     * always set.  Drop bytes that arrive out-of-phase.
     */
    if (pkt_idx == 0u && !(b & 0x08u)) {
        return;
    }

    pkt[pkt_idx++] = b;
    if (pkt_idx < 3u) {
        return;
    }
    pkt_idx = 0u;

    uint8_t status = pkt[0];

    /* Overflow bits (6 and 7) set → discard this packet */
    if (status & 0xC0u) {
        return;
    }

    /*
     * X and Y are 9-bit 2's-complement values.  The 8-bit movement byte
     * holds bits 7:0; bits 4 (X sign) and 5 (Y sign) of the status byte
     * are the 9th bits.  Sign-extend by subtracting 256 when the sign bit
     * is set, because pkt[1]/pkt[2] are unsigned.
     */
    int16_t dx = (int16_t)(uint16_t)pkt[1];
    if (status & 0x10u) { dx -= 256; }   /* X sign bit */

    int16_t dy = (int16_t)(uint16_t)pkt[2];
    if (status & 0x20u) { dy -= 256; }   /* Y sign bit */

    ms.buttons  = status & 0x07u;
    ms.x       += dx;
    ms.y       -= dy;   /* PS/2 Y-positive = up; screen row-positive = down */
    ms.packets++;

    /* Clamp to VGA text-mode screen dimensions */
    if (ms.x < 0)            { ms.x = 0;  }
    if (ms.x >= (int16_t)80) { ms.x = 79; }
    if (ms.y < 0)            { ms.y = 0;  }
    if (ms.y >= (int16_t)25) { ms.y = 24; }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void mouse_init(void)
{
    memset((void *)&ms, 0, sizeof(ms));
    pkt_idx = 0u;

    /* Step 1 – Enable PS/2 Auxiliary Device */
    ps2_wait_write();
    outb(PS2_CMD, 0xA8);

    /*
     * Step 2 – Read the PS/2 controller command byte, set the AUX interrupt
     * enable bit (bit 1), and clear the AUX clock-disable bit (bit 5).
     */
    ps2_wait_write();
    outb(PS2_CMD, 0x20);            /* Read Command Byte */
    uint8_t cmd = mouse_read();
    cmd |=  0x02u;                  /* Enable AUX interrupt (IRQ12) */
    cmd &= ~0x20u;                  /* Clear AUX clock disable */
    ps2_wait_write();
    outb(PS2_CMD, 0x60);            /* Write Command Byte */
    ps2_wait_write();
    outb(PS2_DATA, cmd);

    /* Step 3 – Reset the mouse and discard the self-test response */
    mouse_write(0xFF);              /* Reset */
    mouse_read();                   /* ACK (0xFA) */
    mouse_read();                   /* Self-test OK (0xAA) */
    mouse_read();                   /* Mouse ID (0x00) */

    /* Step 4 – Set default parameters (100 samples/s, 4 counts/mm) */
    mouse_write(0xF6);              /* Set Defaults */
    mouse_read();                   /* ACK */

    /* Step 5 – Enable data reporting (mouse starts sending packets) */
    mouse_write(0xF4);              /* Enable */
    mouse_read();                   /* ACK */

    /* Register our handler on IRQ12 (slave PIC, line 12) */
    irq_register_handler(12u, mouse_irq_handler);
    pic_unmask_irq(12u);
}

mouse_state_t mouse_get_state(void)
{
    /* Copy volatile fields into a non-volatile snapshot */
    mouse_state_t s;
    s.x       = ms.x;
    s.y       = ms.y;
    s.buttons = ms.buttons;
    s.packets = ms.packets;
    return s;
}
