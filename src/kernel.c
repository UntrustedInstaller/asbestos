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
#include "fat.h"
#include "paging.h"

/* Embedded FAT image, linked in by build.sh via objcopy. */
extern const uint8_t _binary_build_fat_part_img_start[];
extern const uint8_t _binary_build_fat_part_img_end[];

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
    serial_puts("fb_addr=");
    serial_put_hex32(bi->fb_addr);
    serial_puts("\n");
	paging_init(bi);
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

        if (ata_probe(0)) {
            serial_puts("ata: master drive present\n");
            if (ata_identify(0, identify_buf) == 0) {
                serial_puts("ata: identify ok\n");
            } else {
                serial_puts("ata: identify failed\n");
            }
        } else {
            serial_puts("ata: no master drive\n");
        }
    }

    /*
     * Initialize the FAT filesystem.
     *
     * Try the on-disk copy first: the FAT partition starts at LBA 265,
     * right after the 128 KiB kernel region. If that is missing or does
     * not contain the expected files (for example when booting from USB,
     * where the kernel cannot reach the boot medium through ATA), fall
     * back to the FAT image embedded in the kernel.
     */
    {
        int fat_ready = 0;

        fat_set_boot_sector(265);
        if (fat_init() == 0 && fat_open("HELLO.TXT") == 0) {
            fat_close();
            fat_ready = 1;
        } else {
            serial_puts("fat: disk image unavailable, using embedded image\n");
            fat_close();

            fat_set_memdisk((uint8_t *)_binary_build_fat_part_img_start,
                            (uint32_t)(_binary_build_fat_part_img_end -
                                       _binary_build_fat_part_img_start));
            fat_set_boot_sector(0);

            if (fat_init() == 0) {
                fat_ready = 1;
            }
        }

        if (fat_ready) {
            serial_puts("fat: ready\n");

            /* Test: open and read a file from FAT */
            char buf[64];
            if (fat_open("HELLO.TXT") == 0) {
                serial_puts("fat: opened HELLO.TXT\n");
                serial_puts("fat: size=");
                serial_put_dec((uint32_t)fat_size());
                serial_puts("\n");
                int32_t n = fat_read(buf, sizeof(buf) - 1);
                if (n > 0) {
                    buf[n] = '\0';
                    serial_puts("fat: content='");
                    serial_puts(buf);
                    serial_puts("'\n");
                }
                fat_close();
            } else {
                serial_puts("fat: failed to open HELLO.TXT\n");
            }
        } else {
            serial_puts("fat: init failed\n");
        }
    }

    shell_run();
}
