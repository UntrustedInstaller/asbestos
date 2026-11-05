#ifndef FAT32_H
#define FAT32_H

#include "types32.h"

/*
 * FAT16 / FAT32 read-only filesystem driver.
 *
 * Reads sectors via ata_read_sectors() from the primary ATA master drive.
 * First pass supports short filenames (8.3) only, no long filename (LFN)
 * support, read-only.
 */

/* Returns 0 on success, -1 on error. */
int fat_init(void);

/* Set the LBA of the FAT boot sector (default 0). Useful when the disk
 * has an MBR and the FAT partition starts at a non-zero LBA. */
void fat_set_boot_sector(uint32_t lba);

/*
 * Open a file by path. Path uses forward slashes, e.g. "SYSTEM/FOO.TXT".
 * Root directory is implied (no leading slash required).
 * Returns 0 on success, -1 if not found or error.
 */
int fat_open(const char *path);

/* Read up to `size` bytes into `buf`. Returns bytes read, or -1 on error. */
int32_t fat_read(void *buf, uint32_t size);

/* Returns the size of the currently open file, or -1 if no file is open. */
int32_t fat_size(void);

/* Close the currently open file. */
void fat_close(void);

/* Returns 1 if a file is currently open, 0 otherwise. */
int fat_is_open(void);

/* List all entries in a directory. Path uses forward slashes, e.g. "SUBDIR".
 * An empty path lists the root directory. */
void fat_list_dir(const char *path);

#endif
