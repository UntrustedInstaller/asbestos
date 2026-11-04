#include "boot_info.h"
#include "console.h"
#include "io.h"
#include "serial.h"

static void fail(const char *message) {
    serial_puts("asbestOS: ");
    serial_puts(message);
    serial_puts("\n");

    for (;;) {
        cpu_halt();
    }
}

void kernel_main(void) {
    serial_init();
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
    console_puts("\n\n");

    console_puts("ABCDEFGHIJKLMNOPQRSTUVWXYZ\n");
    console_puts("abcdefghijklmnopqrstuvwxyz\n");
    console_puts("0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~\n\n");

    console_puts("If you can read this, the kernel is running.\n");
    console_puts("Halting.\n");

    serial_puts("Halting in kernel_main\n");

    for (;;) {
        cpu_halt();
    }
}
