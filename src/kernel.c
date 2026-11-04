#include "boot_info.h"
#include "console.h"
#include "idt.h"
#include "io.h"
#include "keyboard.h"
#include "panic.h"
#include "pic.h"
#include "pit.h"
#include "serial.h"
#include "shell.h"
#include "ata_pio.h"

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

    /* Remap the PIC so IRQs live at 0x20-0x2F, start the PIT at 100 Hz,
     * unmask IRQ 0 (timer) and IRQ 1 (keyboard), then enable interrupts.
     * The keyboard is now IRQ-driven; the shell blocks waiting for input
     * instead of polling. */
    pic_remap(PIC1_OFFSET, PIC2_OFFSET);
    pit_init(100u);
    pic_unmask_irq(IRQ_TIMER);
    pic_unmask_irq(IRQ_KEYBOARD);
    __asm__ __volatile__("sti");

    /* Probe and identify the primary ATA master drive. */
    {
        uint16_t identify_buf[ATA_SECTOR_WORDS];

        if (ata_probe(ATA_DATA, 0)) {
            serial_puts("ata: master drive present\n");
            if (ata_identify(ATA_DATA, 0, identify_buf) == 0) {
                serial_puts("ata: identify ok\n");
            } else {
                serial_puts("ata: identify failed\n");
            }
        } else {
            serial_puts("ata: no master drive\n");
        }
    }

    shell_run();
}
