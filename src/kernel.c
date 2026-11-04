#include "boot_info.h"
#include "console.h"
#include "idt.h"
#include "keyboard.h"
#include "panic.h"
#include "serial.h"
#include "shell.h"

static void fail(const char *message) {
    panic(message, 0);
}

void kernel_main(void) {
    serial_init();
    idt_init();

    serial_puts("asbestOS kernel entered\n");

    const struct BootInfo *bi =
        (const struct BootInfo *)(uint32_t)BOOT_INFO_ADDR;

    if (bi->magic != BOOT_INFO_MAGIC) {
        fail("missing or corrupt BootInfo block");
    }
    if (bi->fb_addr == 0) {
        fail("VBE did not provide a framebuffer address");
    }
    if (bi->bpp != 32) {
        fail("expected a 32-bpp VBE mode");
    }
    if (bi->pitch < (uint32_t)bi->width * 4u) {
        fail("invalid framebuffer pitch");
    }

    console_init(bi);
    console_clear();

    console_puts("asbestOS\n");
    console_puts("difficult. by design.\n\n");

    console_puts("Framebuffer: ");
    console_put_hex32(bi->fb_addr);
    console_puts("\n");

    console_puts("Mode:        ");
    console_put_dec(bi->width);
    console_puts("x");
    console_put_dec(bi->height);
    console_puts("x");
    console_put_dec(bi->bpp);
    console_puts("\n");

    console_puts("Pitch:       ");
    console_put_dec(bi->pitch);
    console_puts(" bytes/scanline\n");

    console_puts("VBE mode:    ");
    console_put_hex32(bi->mode);
    console_puts("\n");

    console_puts("Masks:       R");
    console_put_dec(bi->red_size);
    console_puts("@");
    console_put_dec(bi->red_pos);
    console_puts(" G");
    console_put_dec(bi->green_size);
    console_puts("@");
    console_put_dec(bi->green_pos);
    console_puts(" B");
    console_put_dec(bi->blue_size);
    console_puts("@");
    console_put_dec(bi->blue_pos);
    console_puts("\n");

    keyboard_init();
    shell_run();
}
