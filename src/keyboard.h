#ifndef KEYBOARD32_H
#define KEYBOARD32_H

#include "types32.h"

/*
 * Minimal polled PS/2 keyboard driver, scancode set 1.
 *
 * Returns 0 when no key is ready. Printable keys are returned as ASCII,
 * Backspace as '\b', Enter as '\n', and Tab as '\t'. Key releases, modifiers,
 * and extended keys are consumed internally.
 *
 * This is deliberately polling-only. A later milestone can move it behind
 * IRQ 1 once the PIC is remapped.
 */
char keyboard_poll(void);
void keyboard_init(void);

#endif
