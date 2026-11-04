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
    console_puts("difficult. by design.\n");

    keyboard_init();
    shell_run();
}
