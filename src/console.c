#include "console.h"
#include "font8x16.h"
#include "io.h"
#include "string.h"

/*
 * Framebuffer text console.
 *
 * The logical console is always 80x25 cells. Each cell is 8x16 pixels,
 * matching the IBM VGA bitmap font. This preserves the existing shell
 * geometry while rendering through the VBE linear framebuffer.
 *
 * The 80x25 area is centred in the selected VBE mode. The rest of the
 * screen is filled with the background colour.
 */

static const struct BootInfo *boot;
static uint8_t *framebuffer;
static uint32_t pitch;
static uint32_t screen_width;
static uint32_t screen_height;
static uint32_t bpp;
static uint32_t origin_x;
static uint32_t origin_y;

static uint32_t fg_pixel;
static uint32_t bg_pixel;

static uint32_t cursor_x;
static uint32_t cursor_y;

static char cells[CONSOLE_ROWS][CONSOLE_COLS];

static uint32_t scale_channel(uint32_t value, uint8_t size) {
    uint32_t max = (1u << size) - 1u;
    return (value * max + 127u) / 255u;
}

static uint32_t rgb_to_pixel(uint32_t rgb) {
    uint32_t r = (rgb >> 16) & 0xFFu;
    uint32_t g = (rgb >> 8) & 0xFFu;
    uint32_t b = rgb & 0xFFu;

    r = scale_channel(r, boot->red_size);
    g = scale_channel(g, boot->green_size);
    b = scale_channel(b, boot->blue_size);

    return (r << boot->red_pos) |
           (g << boot->green_pos) |
           (b << boot->blue_pos);
}

static void put_pixel(uint32_t x, uint32_t y, uint32_t pixel) {
    if (x >= screen_width || y >= screen_height) {
        return;
    }

    uint8_t *p = framebuffer + y * pitch + x * (bpp >> 3);

    switch (bpp) {
        case 32:
            *(uint32_t *)p = pixel;
            break;
        case 24:
            p[0] = (uint8_t)(pixel);
            p[1] = (uint8_t)(pixel >> 8);
            p[2] = (uint8_t)(pixel >> 16);
            break;
        case 16:
            *(uint16_t *)p = (uint16_t)pixel;
            break;
        default:
            break;
    }
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t pixel) {
    for (uint32_t yy = 0; yy < h; yy++) {
        for (uint32_t xx = 0; xx < w; xx++) {
            put_pixel(x + xx, y + yy, pixel);
        }
    }
}

static void draw_glyph(char c, uint32_t x, uint32_t y,
                       uint32_t fg, uint32_t bg) {
    uint8_t ch = (uint8_t)c;
    if (ch < 32 || ch > 126) {
        ch = '?';
    }

    const unsigned char *glyph = font8x16[ch - 32];

    for (uint32_t row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (uint32_t col = 0; col < 8; col++) {
            uint32_t pixel = (bits & (0x80u >> col)) ? fg : bg;
            put_pixel(x + col, y + row, pixel);
        }
    }
}

static void draw_cell(uint32_t col, uint32_t row) {
    uint32_t x = origin_x + col * CONSOLE_CELL_W;
    uint32_t y = origin_y + row * CONSOLE_CELL_H;
    char c = cells[row][col];

    draw_glyph(c ? c : ' ', x, y, fg_pixel, bg_pixel);
}

static void draw_cursor(void) {
    uint32_t x = origin_x + cursor_x * CONSOLE_CELL_W;
    uint32_t y = origin_y + cursor_y * CONSOLE_CELL_H;

    for (uint32_t row = CONSOLE_CELL_H - 2; row < CONSOLE_CELL_H; row++) {
        for (uint32_t col = 0; col < CONSOLE_CELL_W; col++) {
            put_pixel(x + col, y + row, fg_pixel);
        }
    }
}

static void erase_cursor(void) {
    draw_cell(cursor_x, cursor_y);
}

static void redraw_all(void) {
    for (uint32_t row = 0; row < CONSOLE_ROWS; row++) {
        for (uint32_t col = 0; col < CONSOLE_COLS; col++) {
            draw_cell(col, row);
        }
    }
}

static void scroll(void) {
    memmove(&cells[0][0], &cells[1][0],
            (CONSOLE_ROWS - 1) * CONSOLE_COLS);

    for (uint32_t col = 0; col < CONSOLE_COLS; col++) {
        cells[CONSOLE_ROWS - 1][col] = ' ';
    }

    redraw_all();
    cursor_y = CONSOLE_ROWS - 1;
}

void console_init(const struct BootInfo *bi) {
    boot = bi;
    framebuffer = (uint8_t *)(uint32_t)bi->fb_addr;
    pitch = bi->pitch;
    screen_width = bi->width;
    screen_height = bi->height;
    bpp = bi->bpp;

    origin_x = (screen_width > CONSOLE_COLS * CONSOLE_CELL_W)
                   ? (screen_width - CONSOLE_COLS * CONSOLE_CELL_W) / 2
                   : 0;
    origin_y = (screen_height > CONSOLE_ROWS * CONSOLE_CELL_H)
                   ? (screen_height - CONSOLE_ROWS * CONSOLE_CELL_H) / 2
                   : 0;

    fg_pixel = rgb_to_pixel(CONSOLE_DEFAULT_FG);
    bg_pixel = rgb_to_pixel(CONSOLE_DEFAULT_BG);

    console_clear();
}

void console_clear(void) {
    fill_rect(0, 0, screen_width, screen_height, bg_pixel);

    for (uint32_t row = 0; row < CONSOLE_ROWS; row++) {
        for (uint32_t col = 0; col < CONSOLE_COLS; col++) {
            cells[row][col] = ' ';
        }
    }

    cursor_x = 0;
    cursor_y = 0;
    draw_cursor();
}

void console_gotoxy(uint32_t col, uint32_t row) {
    if (col >= CONSOLE_COLS) {
        col = CONSOLE_COLS - 1;
    }
    if (row >= CONSOLE_ROWS) {
        row = CONSOLE_ROWS - 1;
    }

    erase_cursor();
    cursor_x = col;
    cursor_y = row;
    draw_cursor();
}

void console_set_color(uint32_t fg_rgb, uint32_t bg_rgb) {
    fg_pixel = rgb_to_pixel(fg_rgb);
    bg_pixel = rgb_to_pixel(bg_rgb);
    console_clear();
}

static void newline(void) {
    cursor_x = 0;
    cursor_y++;
    if (cursor_y >= CONSOLE_ROWS) {
        scroll();
    }
}

void console_putc(char c) {
    erase_cursor();

    switch (c) {
        case '\n':
            newline();
            break;
        case '\r':
            cursor_x = 0;
            break;
        case '\b':
            if (cursor_x > 0) {
                cursor_x--;
            } else if (cursor_y > 0) {
                cursor_y--;
                cursor_x = CONSOLE_COLS - 1;
            }
            cells[cursor_y][cursor_x] = ' ';
            draw_cell(cursor_x, cursor_y);
            break;
        case '\t':
            do {
                cells[cursor_y][cursor_x] = ' ';
                draw_cell(cursor_x, cursor_y);
                cursor_x++;
                if (cursor_x >= CONSOLE_COLS) {
                    newline();
                }
            } while ((cursor_x % 8) != 0);
            break;
        default:
            cells[cursor_y][cursor_x] = c;
            draw_cell(cursor_x, cursor_y);
            cursor_x++;
            if (cursor_x >= CONSOLE_COLS) {
                newline();
            }
            break;
    }

    draw_cursor();
}

void console_puts(const char *s) {
    while (*s) {
        console_putc(*s++);
    }
}

void console_put_hex32(uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";
    console_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        console_putc(hex[(value >> shift) & 0xF]);
    }
}

void console_put_dec(uint32_t value) {
    char buf[11];
    int i = 0;

    if (value == 0) {
        console_putc('0');
        return;
    }
    while (value && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (i--) {
        console_putc(buf[i]);
    }
}
