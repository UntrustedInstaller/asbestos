#ifndef PIT_H
#define PIT_H

#include "types32.h"

/*
 * 8254 Programmable Interval Timer, channel 0.
 *
 * Mode 3 (square wave) produces a regular tick on IRQ 0. The default
 * frequency is 100 Hz; call pit_init() with a different value if needed.
 */

/* Channel 0 data port, mode/command port */
#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43

/* Command byte: channel 0, lobyte/hibyte, mode 3, binary */
#define PIT_CMD_CH0_MODE3 0x36

/* Base timer frequency in Hz */
#define PIT_BASE_FREQ 1193182u

void pit_init(uint32_t frequency_hz);
void pit_irq_handler(void);
uint32_t pit_ticks_get(void);

#endif
