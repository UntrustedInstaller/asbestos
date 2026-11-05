#include "ata_pio.h"
#include "io.h"

/*
 * inw/outw for the 16-bit data port.
 */
static inline uint16_t ata_inw(uint16_t port) {
    uint16_t value;
    __asm__ __volatile__("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void ata_outw(uint16_t port, uint16_t value) {
    __asm__ __volatile__("outw %0, %1" : : "a"(value), "Nd"(port));
}

/*
 * Busy-loop wait on the status register. Returns 0 when the mask matches,
 * -1 on timeout. No PIT yet, so this is a pure spin.
 */
static int ata_wait_mask(uint8_t mask, uint8_t value) {
    for (int i = 0; i < 2000000; i++) {
        uint8_t st = inb(ATA_STATUS);
        if ((st & mask) == value) {
            return 0;
        }
        io_wait();
    }
    return -1;
}

static int ata_wait_not_busy(void) {
    return ata_wait_mask(ATA_SR_BSY, 0);
}

static int ata_wait_drq(void) {
    return ata_wait_mask(ATA_SR_DRQ, ATA_SR_DRQ);
}

static int ata_select_drive(int drive, uint32_t lba) {
    /* 111Lxxxx: LBA bit set, drive select, top 4 LBA bits */
    uint8_t dev = 0xE0 | ATA_DEV_LBA | ((drive & 1) << 4)
                | ((uint8_t)((lba >> 24) & 0x0F));
    outb(ATA_DEVICE, dev);
    io_wait();
    return ata_wait_not_busy();
}

int ata_probe(int drive) {
    outb(ATA_DEVICE, 0xA0 | ((drive & 1) << 4));
    io_wait();
    /* A missing ISA port usually reads back 0xFF. */
    return inb(ATA_STATUS) != 0xFF;
}

int ata_identify(int drive, uint16_t *buf) {
    if (ata_select_drive(drive, 0)) {
        return -1;
    }

    outb(ATA_FEATURES, 0);
    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);

    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);

    if (ata_wait_not_busy()) {
        return -1;
    }
    if (ata_wait_drq()) {
        return -1;
    }

    if (inb(ATA_STATUS) & ATA_SR_ERR) {
        return -1;
    }

    for (int i = 0; i < ATA_SECTOR_WORDS; i++) {
        buf[i] = ata_inw(ATA_DATA);
    }
    return 0;
}

int ata_read_sectors(int drive, uint32_t lba,
                     uint8_t count, void *buf) {
    if (count == 0) {
        return -1;
    }

    if (ata_select_drive(drive, lba)) {
        return -1;
    }

    outb(ATA_FEATURES, 0);
    outb(ATA_SECCOUNT, count);
    outb(ATA_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA2, (uint8_t)((lba >> 16) & 0xFF));

    outb(ATA_COMMAND, ATA_CMD_READ);

    uint16_t *ptr = (uint16_t *)buf;

    for (int sector = 0; sector < count; sector++) {
        if (ata_wait_not_busy()) {
            return -1;
        }
        if (ata_wait_drq()) {
            return -1;
        }

        if (inb(ATA_STATUS) & ATA_SR_ERR) {
            return -1;
        }

        for (int i = 0; i < ATA_SECTOR_WORDS; i++) {
            ptr[sector * ATA_SECTOR_WORDS + i] = ata_inw(ATA_DATA);
        }
    }

    return ata_wait_not_busy() ? -1 : 0;
}

int ata_write_sectors(int drive, uint32_t lba,
                      uint8_t count, const void *buf) {
    if (count == 0) {
        return -1;
    }

    if (ata_select_drive(drive, lba)) {
        return -1;
    }

    outb(ATA_FEATURES, 0);
    outb(ATA_SECCOUNT, count);
    outb(ATA_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA2, (uint8_t)((lba >> 16) & 0xFF));

    outb(ATA_COMMAND, ATA_CMD_WRITE);

    const uint16_t *ptr = (const uint16_t *)buf;

    for (int sector = 0; sector < count; sector++) {
        if (ata_wait_not_busy()) {
            return -1;
        }
        if (ata_wait_drq()) {
            return -1;
        }

        for (int i = 0; i < ATA_SECTOR_WORDS; i++) {
            ata_outw(ATA_DATA,
                     ptr[sector * ATA_SECTOR_WORDS + i]);
        }
    }

    if (ata_wait_not_busy()) {
        return -1;
    }

    if (inb(ATA_STATUS) & ATA_SR_ERR) {
        return -1;
    }
    return 0;
}
