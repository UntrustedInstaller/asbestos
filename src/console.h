#ifndef CONSOLE32_H
#define CONSOLE32_H

#include "boot_info.h"

#define CONSOLE_COLS 80u
#define CONSOLE_ROWS 25u
#define CONSOLE_CELL_W 8u
#define CONSOLE_CELL_H 16u

#define CONSOLE_DEFAULT_BG 0x4B2222u
#define CONSOLE_DEFAULT_FG 0xC39393u

void console_init(const struct BootInfo *bi);
void console_clear(void);
void console_gotoxy(uint32_t col, uint32_t row);
void console_set_color(uint32_t fg_rgb, uint32_t bg_rgb);
void console_putc(char c);
void console_puts(const char *s);
void console_put_hex32(uint32_t value);
void console_put_dec(uint32_t value);

#endif
