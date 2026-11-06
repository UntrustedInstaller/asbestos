#include "keyboard.h"
#include "io.h"
#include "pic.h"
#include "pit.h"

#define PS2_DATA 0x60

#define PS2_STATUS_OUTPUT 0x01

#define SC_LSHIFT 0x2A
#define SC_RSHIFT 0x36
#define SC_CAPSLOCK 0x3A
#define SC_ENTER 0x1C
#define SC_BACKSPACE 0x0E
#define SC_TAB 0x0F
#define SC_EXTENDED 0xE0

struct keyboard_buffer keyboard_buf;

static int shift_down;
static int caps_lock;
static int extended;

/*
 * Scancode set 1, unshifted. Only the keys a shell needs are mapped; everything
 * else falls through as 0. The shifted table supplies symbols, and letters are
 * handled separately so Caps Lock works without a third table.
 */
static const char keymap_lower[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
    [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\', [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c',
    [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = 'm',
    [0x33] = ',', [0x34] = '.', [0x35] = '/', [0x39] = ' ',
};

static const char keymap_upper[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
    [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
    [0x0A] = '(', [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
    [0x1A] = '{', [0x1B] = '}', [0x27] = ':', [0x28] = '"',
    [0x29] = '~', [0x2B] = '|', [0x33] = '<', [0x34] = '>',
    [0x35] = '?', [0x39] = ' ',
};

static uint8_t buffer_pop(void) {
    uint8_t sc;

    cpu_cli();
    if (keyboard_buf.count == 0) {
        cpu_sti();
        return 0;
    }

    sc = keyboard_buf.data[keyboard_buf.tail];
    keyboard_buf.tail = (keyboard_buf.tail + 1u) % KEYBOARD_BUFFER_SIZE;
    keyboard_buf.count--;
    cpu_sti();

    return sc;
}

static void buffer_push(uint8_t sc) {
    cpu_cli();
    if (keyboard_buf.count < KEYBOARD_BUFFER_SIZE) {
        keyboard_buf.data[keyboard_buf.head] = sc;
        keyboard_buf.head = (keyboard_buf.head + 1u) % KEYBOARD_BUFFER_SIZE;
        keyboard_buf.count++;
    }
    cpu_sti();
}

void keyboard_irq_handler(void) {
    uint8_t scancode = inb(PS2_DATA);

    buffer_push(scancode);

    pic_eoi(IRQ_KEYBOARD);
}

void keyboard_init(void) {
    keyboard_buf.head = 0;
    keyboard_buf.tail = 0;
    keyboard_buf.count = 0;

    shift_down = 0;
    caps_lock = 0;
    extended = 0;

    /* Drop anything the firmware left in the output buffer. */
    while (inb(0x64) & PS2_STATUS_OUTPUT) {
        (void)inb(PS2_DATA);
    }
}

int keyboard_scancode_available(void) {
    return keyboard_buf.count > 0;
}

char keyboard_poll(void) {
    uint8_t scancode = buffer_pop();

    if (!scancode) {
        return 0;
    }

    if (scancode == SC_EXTENDED) {
        extended = 1;
        return 0;
    }

    if (extended) {
        extended = 0;
        return 0;
    }

    if (scancode & 0x80) {
        uint8_t released = scancode & 0x7F;
        if (released == SC_LSHIFT || released == SC_RSHIFT) {
            shift_down = 0;
        }
        return 0;
    }

    if (scancode == SC_LSHIFT || scancode == SC_RSHIFT) {
        shift_down = 1;
        return 0;
    }

    switch (scancode) {
        case SC_ENTER:
            return '\n';
        case SC_BACKSPACE:
            return '\b';
        case SC_TAB:
            return '\t';
        case SC_CAPSLOCK:
            caps_lock = !caps_lock;
            return 0;
        default:
            break;
    }

    char c = keymap_lower[scancode];

    if (c >= 'a' && c <= 'z') {
        if (shift_down ^ caps_lock) {
            c = (char)(c - 'a' + 'A');
        }
    } else if (shift_down) {
        c = keymap_upper[scancode];
    }

    return c;
}

char keyboard_read_char(void) {
    while (!keyboard_scancode_available()) {
        cpu_pause();
    }
    return keyboard_poll();
}

/*
 * Block until a character is available or `timeout_ticks` PIT ticks elapse.
 * Returns 0 on timeout (no key pressed within the window).
 */
char keyboard_read_char_timeout(uint32_t timeout_ticks) {
    uint32_t start = pit_ticks_get();

    while (!keyboard_scancode_available()) {
        if (pit_ticks_get() - start >= timeout_ticks) {
            return 0;
        }
        cpu_pause();
    }
    return keyboard_poll();
}
