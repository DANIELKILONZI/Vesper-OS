#include "keyboard.h"
#include "port_io.h"
#include "isr.h"
#include "pic.h"
#include "process.h"

/* -------------------------------------------------------------------------
 * Scancode → ASCII translation tables (PS/2 Scancode Set 1)
 *
 * Index = make-code scancode byte.  Value 0 = no printable mapping.
 * Covers scancodes 0x00–0x39.
 * ---------------------------------------------------------------------- */
static const char scancode_ascii[] = {
    /* 0x00 */ 0,
    /* 0x01 */ 0,     /* Escape        */
    /* 0x02 */ '1',
    /* 0x03 */ '2',
    /* 0x04 */ '3',
    /* 0x05 */ '4',
    /* 0x06 */ '5',
    /* 0x07 */ '6',
    /* 0x08 */ '7',
    /* 0x09 */ '8',
    /* 0x0A */ '9',
    /* 0x0B */ '0',
    /* 0x0C */ '-',
    /* 0x0D */ '=',
    /* 0x0E */ '\b',  /* Backspace     */
    /* 0x0F */ '\t',  /* Tab           */
    /* 0x10 */ 'q',
    /* 0x11 */ 'w',
    /* 0x12 */ 'e',
    /* 0x13 */ 'r',
    /* 0x14 */ 't',
    /* 0x15 */ 'y',
    /* 0x16 */ 'u',
    /* 0x17 */ 'i',
    /* 0x18 */ 'o',
    /* 0x19 */ 'p',
    /* 0x1A */ '[',
    /* 0x1B */ ']',
    /* 0x1C */ '\n',  /* Enter         */
    /* 0x1D */ 0,     /* Left Ctrl     */
    /* 0x1E */ 'a',
    /* 0x1F */ 's',
    /* 0x20 */ 'd',
    /* 0x21 */ 'f',
    /* 0x22 */ 'g',
    /* 0x23 */ 'h',
    /* 0x24 */ 'j',
    /* 0x25 */ 'k',
    /* 0x26 */ 'l',
    /* 0x27 */ ';',
    /* 0x28 */ '\'',
    /* 0x29 */ '`',
    /* 0x2A */ 0,     /* Left Shift    */
    /* 0x2B */ '\\',
    /* 0x2C */ 'z',
    /* 0x2D */ 'x',
    /* 0x2E */ 'c',
    /* 0x2F */ 'v',
    /* 0x30 */ 'b',
    /* 0x31 */ 'n',
    /* 0x32 */ 'm',
    /* 0x33 */ ',',
    /* 0x34 */ '.',
    /* 0x35 */ '/',
    /* 0x36 */ 0,     /* Right Shift   */
    /* 0x37 */ '*',   /* Keypad *      */
    /* 0x38 */ 0,     /* Left Alt      */
    /* 0x39 */ ' ',   /* Space         */
};

static const char scancode_ascii_shift[] = {
    /* 0x00 */ 0,
    /* 0x01 */ 0,
    /* 0x02 */ '!',
    /* 0x03 */ '@',
    /* 0x04 */ '#',
    /* 0x05 */ '$',
    /* 0x06 */ '%',
    /* 0x07 */ '^',
    /* 0x08 */ '&',
    /* 0x09 */ '*',
    /* 0x0A */ '(',
    /* 0x0B */ ')',
    /* 0x0C */ '_',
    /* 0x0D */ '+',
    /* 0x0E */ '\b',
    /* 0x0F */ '\t',
    /* 0x10 */ 'Q',
    /* 0x11 */ 'W',
    /* 0x12 */ 'E',
    /* 0x13 */ 'R',
    /* 0x14 */ 'T',
    /* 0x15 */ 'Y',
    /* 0x16 */ 'U',
    /* 0x17 */ 'I',
    /* 0x18 */ 'O',
    /* 0x19 */ 'P',
    /* 0x1A */ '{',
    /* 0x1B */ '}',
    /* 0x1C */ '\n',
    /* 0x1D */ 0,
    /* 0x1E */ 'A',
    /* 0x1F */ 'S',
    /* 0x20 */ 'D',
    /* 0x21 */ 'F',
    /* 0x22 */ 'G',
    /* 0x23 */ 'H',
    /* 0x24 */ 'J',
    /* 0x25 */ 'K',
    /* 0x26 */ 'L',
    /* 0x27 */ ':',
    /* 0x28 */ '"',
    /* 0x29 */ '~',
    /* 0x2A */ 0,
    /* 0x2B */ '|',
    /* 0x2C */ 'Z',
    /* 0x2D */ 'X',
    /* 0x2E */ 'C',
    /* 0x2F */ 'V',
    /* 0x30 */ 'B',
    /* 0x31 */ 'N',
    /* 0x32 */ 'M',
    /* 0x33 */ '<',
    /* 0x34 */ '>',
    /* 0x35 */ '?',
    /* 0x36 */ 0,
    /* 0x37 */ '*',
    /* 0x38 */ 0,
    /* 0x39 */ ' ',
};

/* -------------------------------------------------------------------------
 * Ring buffer  (interrupt-driven producer / consumer)
 *
 * The IRQ1 handler writes characters here; keyboard_getchar() reads them.
 * Declared volatile to prevent the compiler from caching reads/writes.
 * ---------------------------------------------------------------------- */
#define KB_BUF_SIZE 64u

static volatile char    kb_buf[KB_BUF_SIZE];
static volatile uint8_t kb_head = 0;   /* consumer read  position */
static volatile uint8_t kb_tail = 0;   /* producer write position */

/* Push one ASCII character into the ring buffer (called from IRQ context) */
static void kb_push(char c)
{
    uint8_t next = (uint8_t)((kb_tail + 1u) % KB_BUF_SIZE);
    if (next != kb_head) {          /* drop silently if the buffer is full */
        kb_buf[kb_tail] = c;
        kb_tail         = next;
    }
    /* Wake only processes that blocked waiting for keyboard input */
    process_wake_reason(WAIT_KEYBOARD);
}

/* -------------------------------------------------------------------------
 * Driver state
 * ---------------------------------------------------------------------- */
static int     shift_pressed = 0;
static int     extended_code = 0;   /* set when the 0xE0 prefix byte was seen */

/* -------------------------------------------------------------------------
 * IRQ1 handler – called from the interrupt dispatcher in isr.c
 *
 * Reads the PS/2 scancode, handles the 0xE0 extended-scancode prefix,
 * updates Shift state, translates make-codes to ASCII (or KEY_* constants
 * for special keys) and pushes the result into the ring buffer.
 * ---------------------------------------------------------------------- */
static void keyboard_irq_handler(registers_t *regs)
{
    (void)regs;

    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    /* Extended-scancode prefix: next byte is an extended scancode */
    if (scancode == 0xE0u) {
        extended_code = 1;
        return;
    }

    if (extended_code) {
        extended_code = 0;

        /* Only handle make-codes (bit 7 clear) for extended keys */
        if (!(scancode & 0x80u)) {
            char special = 0;
            switch (scancode) {
            case 0x48u: special = (char)KEY_UP;    break;  /* ↑ */
            case 0x50u: special = (char)KEY_DOWN;  break;  /* ↓ */
            case 0x4Bu: special = (char)KEY_LEFT;  break;  /* ← */
            case 0x4Du: special = (char)KEY_RIGHT; break;  /* → */
            default:    break;
            }
            if (special) {
                kb_push(special);
            }
        }
        return;
    }

    /* Break codes (bit 7 set) indicate key release */
    if (scancode & 0x80u) {
        uint8_t released = scancode & 0x7Fu;
        if (released == 0x2Au || released == 0x36u) {
            shift_pressed = 0;
        }
        return;
    }

    /* Track Shift key press */
    if (scancode == 0x2Au || scancode == 0x36u) {
        shift_pressed = 1;
        return;
    }

    /* Translate scancode to ASCII and buffer it */
    if (scancode < (uint8_t)sizeof(scancode_ascii)) {
        char c = shift_pressed
                 ? scancode_ascii_shift[scancode]
                 : scancode_ascii[scancode];
        if (c != 0) {
            kb_push(c);
        }
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void keyboard_init(void)
{
    shift_pressed = 0;
    extended_code = 0;
    kb_head       = 0;
    kb_tail       = 0;

    /* Register our IRQ handler and unmask IRQ1 on the master PIC */
    irq_register_handler(1, keyboard_irq_handler);
    pic_unmask_irq(1);
}

char keyboard_getchar(void)
{
    /* Yield to other processes while the buffer is empty */
    while (kb_head == kb_tail) {
        if (current_process) {
            process_block_on(WAIT_KEYBOARD);   /* targeted wakeup */
        } else {
            __asm__ volatile ("hlt");
        }
    }

    char c  = kb_buf[kb_head];
    kb_head = (uint8_t)((kb_head + 1u) % KB_BUF_SIZE);
    return c;
}
