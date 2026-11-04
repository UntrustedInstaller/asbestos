#ifndef PANIC32_H
#define PANIC32_H

#include "idt.h"

/*
 * Print a panic screen and stop the machine. The trap frame may be NULL for
 * panics that are not caused by a CPU exception.
 *
 * This never returns.
 */
void panic(const char *reason, const struct trap_frame *frame)
    __attribute__((noreturn));

#endif
