#ifndef ATA_PIO_H
#define ATA_PIO_H

#include "types32.h"

/*
 * ATA PIO driver, primary channel.
 *
 * Polling-only first pass: no IRQ handler, no DMA, LBA28 addressing.
 * Target machines are assumed to present a standard IDE controller at
 * 0x1F0 (data/command/status) and 0x3F6 (control/alt status).
 */

/* Primary channel registers */
#define ATA_DATA      0x1F0
#define ATA_ERROR     0x1F1
#define ATA_FEATURES  0x1F1
#define ATA_SECCOUNT  0x1F2
#define ATA_LBA0      0x1F3
#define ATA_LBA1      0x1F4
#define ATA_LBA2      0x1F5
#define ATA_DEVICE    0x1F6
#define ATA_COMMAND   0x1F7
#define ATA_STATUS    0x1F7

/* Alternate status / device control */
#define ATA_ALTSTATUS 0x3F6
#define ATA_CONTROL   0x3F6

/* Device/head register bits */
#define ATA_DEV_LBA   0x40   /* 1 = LBA mode, 0 = CHS */
#define ATA_DEV_DRIVE 0x10   /* 1 = slave, 0 = master */

/* Status register bits */
#define ATA_SR_ERR    0x01
#define ATA_SR_IDX    0x02
#define ATA_SR_CORR   0x04
#define ATA_SR_DRQ    0x08
#define ATA_SR_DSC    0x10
#define ATA_SR_DWF    0x20
#define ATA_SR_DRDY   0x40
#define ATA_SR_BSY    0x80

/* Error register bits */
#define ATA_ER_BBK    0x80
#define ATA_ER_UNC    0x40
#define ATA_ER_MC     0x20
#define ATA_ER_IDNF   0x10
#define ATA_ER_MCR    0x08
#define ATA_ER_ABRT   0x04
#define ATA_ER_TK0NF  0x02
#define ATA_ER_AMNF   0x01

/* Commands */
#define ATA_CMD_IDENTIFY   0xEC
#define ATA_CMD_READ       0x20
#define ATA_CMD_WRITE      0x30
#define ATA_CMD_FLUSH      0xE7

/* Sector geometry */
#define ATA_SECTOR_SIZE  512
#define ATA_SECTOR_WORDS (ATA_SECTOR_SIZE / 2)

/* Returns 0 on success, -1 on error. */
int ata_probe(int drive);
int ata_identify(int drive, uint16_t *buf);
int ata_read_sectors(int drive, uint32_t lba,
                     uint8_t count, void *buf);
int ata_write_sectors(int drive, uint32_t lba,
                      uint8_t count, const void *buf);

#endif
