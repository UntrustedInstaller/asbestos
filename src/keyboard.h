#ifndef KEYBOARD32_H
#define KEYBOARD32_H

#include "types32.h"

/*
 * Polled or IRQ-driven PS/2 keyboard, scancode set 1.
 *
 * Raw scancodes are collected into a small buffer either by polling or by
 * the IRQ 1 handler. keyboard_poll() translates raw scancodes to ASCII and
 * returns 0 when nothing is pending. keyboard_read_char() blocks until a
 * character is available.
 */

#define KEYBOARD_BUFFER_SIZE 16

struct keyboard_buffer {
    uint8_t data[KEYBOARD_BUFFER_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t count;
};

extern struct keyboard_buffer keyboard_buf;

void keyboard_init(void);
int keyboard_scancode_available(void);
char keyboard_poll(void);
char keyboard_read_char(void);

/*
 * Block until a character is available or `timeout_ticks` PIT ticks elapse.
 * Returns 0 on timeout (no key pressed within the window).
 */
char keyboard_read_char_timeout(uint32_t timeout_ticks);

/* Called from isr_dispatch() on IRQ 1 (vector 33). */
void keyboard_irq_handler(void);

#endif
