#include "pit.h"
#include "pic.h"
#include "io.h"
#include "serial.h"

/* Ticks since PIT was initialized. Accessed from IRQ context only. */
static volatile uint32_t pit_ticks;

void pit_init(uint32_t frequency_hz) {
    uint32_t divisor;

    if (frequency_hz == 0 || frequency_hz > PIT_BASE_FREQ) {
        frequency_hz = 100u;
    }

    divisor = PIT_BASE_FREQ / frequency_hz;

    /* Command byte to select channel 0, mode 3, binary */
    outb(PIT_COMMAND, PIT_CMD_CH0_MODE3);
    io_wait();

    /* Divisor low byte, then high byte */
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFFu));
    io_wait();
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFFu));
    io_wait();

    pit_ticks = 0;

    serial_puts("pit: initialized at ");
    serial_put_dec(frequency_hz);
    serial_puts(" Hz\n");
}

uint32_t pit_ticks_get(void) {
    return pit_ticks;
}
