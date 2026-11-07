#ifndef FAT32_H
#define FAT32_H

#include "types32.h"
/* Directory entry attribute flags. */
#define ATTR_READONLY 0x01
#define ATTR_HIDDEN   0x02
#define ATTR_SYSTEM   0x04
#define ATTR_VOLUME   0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE  0x20
#define ATTR_LFN        0x0F

/*
 * FAT16 / FAT32 read-only filesystem driver.
 *
 * Reads sectors from the primary ATA master drive, or from an in-memory
 * image installed with fat_set_memdisk(). Supports FAT12, FAT16 and FAT32
 * with short filenames (8.3) only; no long filename (LFN) support, and
 * read-only.
 */

/* Returns 0 on success, -1 on error. */
int fat_init(void);

/* Set the LBA of the FAT boot sector (default 0). Useful when the disk
 * has an MBR and the FAT partition starts at a non-zero LBA. */
void fat_set_boot_sector(uint32_t lba);

/* Serve all sector reads from an in-memory FAT image instead of the ATA
 * disk. Pass NULL to go back to ATA reads. This lets the kernel read a
 * filesystem embedded in the kernel image, which works no matter how the
 * machine booted (USB, AHCI, legacy IDE). Writes also target the memdisk
 * when it is set; ATA remains read-only. */
void fat_set_memdisk(void *base, uint32_t size);

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
 * An empty path lists the root directory. If long_format is non-zero, print
 * entry sizes (Unix `ls -l` style). Returns 0 on success, -1 on error. */
int fat_list_dir(const char *path, int long_format);

/* Returns 1 if path names an existing directory (the empty path is the
 * root), 0 otherwise. */
int fat_is_dir(const char *path);

/* Create a directory entry at `path` with the given attributes and starting
 * cluster. FAT12/FAT16 only. Returns 0 on success, -1 on error. */
int fat_create_dir_entry(const char *path, uint8_t attr,
                         uint32_t start_cluster);

/* Allocate a free cluster in the FAT. Marks it as end-of-chain.
 * Returns the cluster number, or 0xFFFFFFFF on error. FAT12 only. */
uint32_t fat_alloc_cluster(void);

/* Initialize a new directory with `.` and `..` entries.
 * Writes to the directory's first cluster sector. FAT12/FAT16 only.
 * Returns 0 on success, -1 on error. */
int fat_init_dir(uint32_t dir_cluster, uint32_t parent_cluster);

/* Free a cluster chain in the FAT. FAT12 only. Returns 0 on success, -1 on error. */
int fat_free_cluster_chain(uint32_t cluster);

/* Delete a directory entry at `path`. Marks the entry as deleted and frees
 * its cluster chain if it is a directory. FAT12/FAT16 only. Returns 0 on
 * success, -1 on error. */
int fat_delete_dir_entry(const char *path);

/* Look up the starting cluster of a directory. Returns 0 on success,
 * -1 on error. On success, *cluster is set to the directory's cluster
 * (0 for the root directory). */
int fat_get_dir_cluster(const char *path, uint32_t *cluster);

#endif
