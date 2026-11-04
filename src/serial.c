#include "serial.h"
#include "io.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);   /* disable UART interrupts */
    outb(COM1 + 3, 0x80);   /* enable DLAB */
    outb(COM1 + 0, 0x03);   /* divisor low: 38400 baud */
    outb(COM1 + 1, 0x00);   /* divisor high */
    outb(COM1 + 3, 0x03);   /* 8 bits, no parity, one stop bit */
    outb(COM1 + 2, 0xC7);   /* FIFO enable, clear, 14-byte threshold */
    outb(COM1 + 4, 0x0B);   /* IRQs enabled, RTS/DSR set */
}

static int serial_tx_ready(void) {
    return (inb(COM1 + 5) & 0x20) != 0;
}

void serial_putc(char c) {
    while (!serial_tx_ready()) {
        /* wait for the transmit holding register */
    }
    outb(COM1, (uint8_t)c);
}

void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            serial_putc('\r');
        }
        serial_putc(*s++);
    }
}

void serial_put_hex32(uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        serial_putc(hex[(value >> shift) & 0xF]);
    }
}

void serial_put_dec(uint32_t value) {
    char buf[11];
    int i = 0;

    if (value == 0) {
        serial_putc('0');
        return;
    }
    while (value && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (i--) {
        serial_putc(buf[i]);
    }
}
