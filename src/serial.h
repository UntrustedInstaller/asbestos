#ifndef SERIAL32_H
#define SERIAL32_H

#include "types32.h"

void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *s);
void serial_put_hex32(uint32_t value);
void serial_put_dec(uint32_t value);

#endif
