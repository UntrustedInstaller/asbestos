#include "fat.h"
#include "ata_pio.h"
#include "string.h"
#include "console.h"
#include "serial.h"

#define ATA_DRIVE 0    /* primary master */

/* ------------------------------------------------------------------ */
/*  BPB (BIOS Parameter Block) field offsets                          */
/* ------------------------------------------------------------------ */

#define FAT_BYTES_PER_SECTOR   11
#define FAT_SECTORS_PER_CLUSTER 13
#define FAT_RESERVED_SECTORS   14
#define FAT_NUM_FATS           16
#define FAT_ROOT_ENTRIES       17
#define FAT_TOTAL_SECTORS16    19
#define FAT_MEDIA_DESCRIPTOR   21
#define FAT_SECTORS_PER_FAT16  22
#define FAT_SECTORS_PER_TRACK  24
#define FAT_NUM_HEADS          26
#define FAT_HIDDEN_SECTORS     28
#define FAT_TOTAL_SECTORS32    32

/* FAT32 extended BPB */
#define FAT32_SECTORS_PER_FAT  36
#define FAT32_ROOT_CLUSTER     44

/* ------------------------------------------------------------------ */
/*  Directory entry field offsets                                     */
/* ------------------------------------------------------------------ */

#define DIR_NAME        0
#define DIR_ATTR        11
#define DIR_FST_CLUS_HI 20
#define DIR_FST_CLUS_LO 26
#define DIR_FILE_SIZE   28
#define DIR_ENTRY_SIZE  32

#define ATTR_DIRECTORY  0x10
#define ATTR_LFN        0x0F

#define DIR_FREE        0x00
#define DIR_DELETED     0xE5

/* ------------------------------------------------------------------ */
/*  Global state                                                      */
/* ------------------------------------------------------------------ */

static uint16_t bytes_per_sector = 512;
static uint8_t  sectors_per_cluster = 1;
static uint16_t reserved_sector_count = 1;
static uint8_t  num_fats = 2;
static uint32_t sectors_per_fat = 0;
static uint32_t fat_start_sector = 0;
static uint32_t data_start_sector = 0;
static uint32_t root_dir_sector = 0;
static uint32_t root_dir_entries = 0;
static uint32_t root_dir_size_sectors = 0;
static uint32_t cluster_size_bytes = 0;
static int      is_fat12 = 0;
static int      is_fat32 = 0;

/* LBA of the FAT boot sector (0 = sector 0 of the disk). */
static uint32_t fat_boot_sector = 0;

/* Optional in-memory disk image. When non-NULL, sector reads come from
 * here instead of the ATA disk. Writes go here too when non-NULL. */
static uint8_t *memdisk_base = 0;
static uint32_t memdisk_sectors = 0;

/* Current open file state */
static uint32_t current_cluster = 0;
static uint32_t current_offset = 0;
static uint32_t current_size = 0;
static int      file_open = 0;

/* Sector buffers */
static uint8_t sector_buf[512];
static uint8_t fat_sector_buf[512];  /* second buffer for FAT sector straddling */

/* ------------------------------------------------------------------ */
/*  Low-level helpers                                                 */
/* ------------------------------------------------------------------ */

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int read_sector(uint32_t lba, void *buf) {
    if (memdisk_base != 0) {
        const uint8_t *src;
        uint8_t *dst = (uint8_t *)buf;
        uint32_t i;

        if (lba >= memdisk_sectors) {
            return -1;
        }

        src = memdisk_base + (lba * 512u);
        for (i = 0; i < 512u; i++) {
            dst[i] = src[i];
        }
        return 0;
    }

    return ata_read_sectors(ATA_DRIVE, lba, 1, buf);
}

/* Write a sector. Only the in-memory disk is writable; ATA is read-only. */
static int write_sector(uint32_t lba, const void *buf) {
    const uint8_t *src = (const uint8_t *)buf;
    uint8_t *dst;
    uint32_t i;

    if (memdisk_base != 0) {
        if (lba >= memdisk_sectors) {
            return -1;
        }
        dst = memdisk_base + (lba * 512u);
        for (i = 0; i < 512u; i++) {
            dst[i] = src[i];
        }
        return 0;
    }

    /* ATA PIO is read-only in this driver. */
    (void)lba;
    (void)src;
    return -1;
}

/* Convert cluster number to LBA of the first sector of that cluster.
 * data_start_sector already includes the fat_boot_sector offset. */
static uint32_t cluster_to_lba(uint32_t cluster) {
    return data_start_sector + ((cluster - 2) * sectors_per_cluster);
}

/* ------------------------------------------------------------------ */
/*  FAT chain traversal                                               */
/* ------------------------------------------------------------------ */

/* Read the next cluster in the FAT chain. Returns 0xFFFFFFFF on EOC or error. */
static uint32_t fat_next_cluster(uint32_t cluster) {
    uint32_t fat_offset;
    uint32_t fat_sector;
    uint32_t fat_entry_offset;

    if (is_fat12) {
        uint16_t word;
        uint16_t next;

        /* FAT12 packs two 12-bit entries into every three bytes. */
        fat_offset = cluster + (cluster >> 1);
        fat_sector = fat_start_sector + (fat_offset / bytes_per_sector);
        fat_entry_offset = fat_offset % bytes_per_sector;

        if (read_sector(fat_sector, sector_buf)) {
            return 0xFFFFFFFF;
        }

        if (fat_entry_offset == (uint32_t)(bytes_per_sector - 1)) {
            /* The entry straddles this sector and the next one. */
            uint8_t lo = sector_buf[fat_entry_offset];
            if (read_sector(fat_sector + 1, sector_buf)) {
                return 0xFFFFFFFF;
            }
            word = (uint16_t)lo | ((uint16_t)sector_buf[0] << 8);
        } else {
            word = read_u16(&sector_buf[fat_entry_offset]);
        }

        if (cluster & 1u) {
            next = (uint16_t)(word >> 4);
        } else {
            next = (uint16_t)(word & 0x0FFFu);
        }

        if (next >= 0x0FF8u) {
            return 0xFFFFFFFF;  /* EOC */
        }
        return next;
    }

    if (is_fat32) {
        fat_offset = cluster * 4;
    } else {
        fat_offset = cluster * 2;
    }

    fat_sector = fat_start_sector + (fat_offset / bytes_per_sector);
    fat_entry_offset = fat_offset % bytes_per_sector;

    if (read_sector(fat_sector, sector_buf)) {
        return 0xFFFFFFFF;
    }

    if (is_fat32) {
        uint32_t next = read_u32(&sector_buf[fat_entry_offset]) & 0x0FFFFFFFu;
        if (next >= 0x0FFFFFF8u) {
            return 0xFFFFFFFF;  /* EOC */
        }
        return next;
    } else {
        uint16_t next = read_u16(&sector_buf[fat_entry_offset]);
        if (next >= 0xFFF8u) {
            return 0xFFFFFFFF;  /* EOC */
        }
        return next;
    }
}

/* Write a FAT entry value for `cluster`. Returns 0 on success, -1 on error.
 * FAT12 only for now. */
static int fat_write_fat_entry(uint32_t cluster, uint16_t value) {
    if (!is_fat12) {
        return -1;
    }

    uint32_t fat_offset = cluster + (cluster >> 1);
    uint32_t fat_sector = fat_start_sector + (fat_offset / bytes_per_sector);
    uint32_t fat_entry_offset = fat_offset % bytes_per_sector;

    if (read_sector(fat_sector, sector_buf)) {
        return -1;
    }

    int straddle = (fat_entry_offset == (uint32_t)(bytes_per_sector - 1));
    uint16_t word;

    if (straddle) {
        if (read_sector(fat_sector + 1, fat_sector_buf)) {
            return -1;
        }
        word = (uint16_t)sector_buf[fat_entry_offset] |
               ((uint16_t)fat_sector_buf[0] << 8);
    } else {
        word = read_u16(&sector_buf[fat_entry_offset]);
    }

    /* Insert the 12-bit value into the word. */
    uint16_t new_word;
    if (cluster & 1u) {
        new_word = (uint16_t)((word & 0x000Fu) | (value << 4));
    } else {
        new_word = (uint16_t)((word & 0xF000u) | value);
    }

    /* Write back to the FAT sector(s). */
    if (straddle) {
        sector_buf[fat_entry_offset] = (uint8_t)(new_word & 0xFFu);
        if (write_sector(fat_sector, sector_buf)) {
            return -1;
        }
        fat_sector_buf[0] = (uint8_t)(new_word >> 8);
        if (write_sector(fat_sector + 1, fat_sector_buf)) {
            return -1;
        }
    } else {
        sector_buf[fat_entry_offset] = (uint8_t)(new_word & 0xFFu);
        sector_buf[fat_entry_offset + 1] = (uint8_t)(new_word >> 8);
        if (write_sector(fat_sector, sector_buf)) {
            return -1;
        }
    }

    return 0;
}

/* Allocate a free cluster in the FAT. Marks it as end-of-chain.
 * Returns the cluster number, or 0xFFFFFFFF on error. FAT12 only. */
uint32_t fat_alloc_cluster(void) {
    if (!is_fat12) {
        return 0xFFFFFFFF;
    }

    /* Clusters 0 and 1 are reserved; scan from cluster 2. */
    uint32_t max_clusters = (sectors_per_fat * bytes_per_sector * 2u) / 3u;

    for (uint32_t cluster = 2; cluster < max_clusters; cluster++) {
        uint32_t fat_offset = cluster + (cluster >> 1);
        uint32_t fat_sector = fat_start_sector + (fat_offset / bytes_per_sector);
        uint32_t fat_entry_offset = fat_offset % bytes_per_sector;

        if (read_sector(fat_sector, sector_buf)) {
            return 0xFFFFFFFF;
        }

        /* Handle entry straddling two sectors. */
        int straddle = (fat_entry_offset == (uint32_t)(bytes_per_sector - 1));
        uint16_t word;

        if (straddle) {
            if (read_sector(fat_sector + 1, fat_sector_buf)) {
                return 0xFFFFFFFF;
            }
            word = (uint16_t)sector_buf[fat_entry_offset] |
                   ((uint16_t)fat_sector_buf[0] << 8);
        } else {
            word = read_u16(&sector_buf[fat_entry_offset]);
        }

        /* Extract the 12-bit entry for this cluster. */
        uint16_t entry;
        if (cluster & 1u) {
            entry = (uint16_t)(word >> 4);
        } else {
            entry = (uint16_t)(word & 0x0FFFu);
        }

        if (entry != 0x000u) {
            continue;  /* Not free */
        }

        /* Mark as end-of-chain (0xFFF). */
        uint16_t new_value = 0x0FFFu;
        uint16_t new_word;

        if (cluster & 1u) {
            new_word = (uint16_t)((word & 0x000Fu) | (new_value << 4));
        } else {
            new_word = (uint16_t)((word & 0xF000u) | new_value);
        }

        /* Write back to the FAT sector(s). */
        if (straddle) {
            sector_buf[fat_entry_offset] = (uint8_t)(new_word & 0xFFu);
            if (write_sector(fat_sector, sector_buf)) {
                return 0xFFFFFFFF;
            }
            fat_sector_buf[0] = (uint8_t)(new_word >> 8);
            if (write_sector(fat_sector + 1, fat_sector_buf)) {
                return 0xFFFFFFFF;
            }
        } else {
            sector_buf[fat_entry_offset] = (uint8_t)(new_word & 0xFFu);
            sector_buf[fat_entry_offset + 1] = (uint8_t)(new_word >> 8);
            if (write_sector(fat_sector, sector_buf)) {
                return 0xFFFFFFFF;
            }
        }

        return cluster;
    }

    return 0xFFFFFFFF;  /* Disk full */
}

/* Free a cluster chain in the FAT. Sets each entry to 0 (free).
 * FAT12 only. Returns 0 on success, -1 on error. */
int fat_free_cluster_chain(uint32_t cluster) {
    if (!is_fat12 || cluster < 2u) {
        return -1;
    }

    while (cluster != 0xFFFFFFFFu) {
        uint32_t next = fat_next_cluster(cluster);

        uint32_t fat_offset = cluster + (cluster >> 1);
        uint32_t fat_sector = fat_start_sector + (fat_offset / bytes_per_sector);
        uint32_t fat_entry_offset = fat_offset % bytes_per_sector;

        if (read_sector(fat_sector, sector_buf)) {
            return -1;
        }

        int straddle = (fat_entry_offset == (uint32_t)(bytes_per_sector - 1));
        uint16_t word;

        if (straddle) {
            if (read_sector(fat_sector + 1, fat_sector_buf)) {
                return -1;
            }
            word = (uint16_t)sector_buf[fat_entry_offset] |
                   ((uint16_t)fat_sector_buf[0] << 8);
        } else {
            word = read_u16(&sector_buf[fat_entry_offset]);
        }

        /* Clear the 12-bit entry to 0 (free). */
        uint16_t new_word;
        if (cluster & 1u) {
            new_word = (uint16_t)(word & 0x000Fu);  /* preserve low 4 bits */
        } else {
            new_word = (uint16_t)(word & 0xF000u);  /* preserve high 4 bits */
        }

        if (straddle) {
            sector_buf[fat_entry_offset] = (uint8_t)(new_word & 0xFFu);
            if (write_sector(fat_sector, sector_buf)) {
                return -1;
            }
            fat_sector_buf[0] = (uint8_t)(new_word >> 8);
            if (write_sector(fat_sector + 1, fat_sector_buf)) {
                return -1;
            }
        } else {
            sector_buf[fat_entry_offset] = (uint8_t)(new_word & 0xFFu);
            sector_buf[fat_entry_offset + 1] = (uint8_t)(new_word >> 8);
            if (write_sector(fat_sector, sector_buf)) {
                return -1;
            }
        }

        cluster = next;
    }

    return 0;
}

static int parse_component(const char *path, char *out_name);
static int scan_dir_for_name(uint32_t start_sector, uint32_t total_entries,
                             const char *name_11, uint32_t *found_cluster,
                             uint32_t *found_size, uint8_t *found_attr);
static int scan_fat32_dir_chain(uint32_t start_cluster, const char *name_11,
                                uint32_t *found_cluster, uint32_t *found_size,
                                uint8_t *found_attr);
static void write_dir_entry(uint8_t *ent, const char *name_11, uint8_t attr,
                            uint32_t start_cluster);

/* Create a directory entry at `path` with the given attributes and starting
 * cluster. Walks all but the last path component to find the parent
 * directory, then appends a new entry. FAT12/FAT16 only for now.
 * Returns 0 on success, -1 on error. */
int fat_create_dir_entry(const char *path, uint8_t attr,
                         uint32_t start_cluster) {
    char name_11[11];
    int pos = 0;
    uint32_t cur_cluster = 0;
    int in_dir = 0;

    /* Set starting directory */
    if (is_fat32) {
        uint8_t bs[512];
        if (read_sector(fat_boot_sector, bs)) {
            return -1;
        }
        cur_cluster = read_u32(&bs[FAT32_ROOT_CLUSTER]);
    } else {
        cur_cluster = 0;
    }

    /* Walk all but the last path component to reach the parent directory */
    while (path[pos]) {
        int consumed = parse_component(&path[pos], name_11);
        int next_pos = pos + consumed;

        /* Last component: this is the name to create */
        if (path[next_pos] == '\0') {
            break;
        }

        uint32_t found_cluster = 0;
        uint32_t found_size = 0;
        uint8_t found_attr = 0;
        int found;

        if (is_fat32 || !in_dir) {
            found = scan_dir_for_name(root_dir_sector, root_dir_entries,
                                      name_11, &found_cluster, &found_size,
                                      &found_attr);
            in_dir = 1;
        } else {
            found = scan_fat32_dir_chain(cur_cluster, name_11,
                                         &found_cluster, &found_size,
                                         &found_attr);
        }

        if (found != 0 || !(found_attr & ATTR_DIRECTORY)) {
            return -1;  /* Parent not found or not a directory */
        }

        cur_cluster = found_cluster;
        pos = next_pos;
    }

    /* Parse final component as the new entry name */
    parse_component(&path[pos], name_11);

    /* Check the name does not already exist in the parent */
    {
        uint32_t found_cluster = 0;
        uint32_t found_size = 0;
        uint8_t found_attr = 0;
        int found;

        if (is_fat32 || cur_cluster != 0) {
            found = scan_fat32_dir_chain(cur_cluster, name_11,
                                         &found_cluster, &found_size,
                                         &found_attr);
        } else {
            found = scan_dir_for_name(root_dir_sector, root_dir_entries,
                                      name_11, &found_cluster, &found_size,
                                      &found_attr);
        }

        if (found == 0) {
            return -1;  /* Entry already exists */
        }
    }

    /* Scan the parent directory for a free slot (DIR_FREE or DIR_DELETED). */
    if (!is_fat32 && cur_cluster == 0) {
        /* FAT12/FAT16 root directory: linear region */
        uint32_t sector;
        for (sector = 0; sector < root_dir_size_sectors; sector++) {
            uint32_t e;
            if (read_sector(root_dir_sector + sector, sector_buf)) {
                return -1;
            }
            for (e = 0; e < bytes_per_sector / DIR_ENTRY_SIZE; e++) {
                uint8_t *ent = &sector_buf[e * DIR_ENTRY_SIZE];
                if (ent[DIR_NAME] == DIR_FREE ||
                    ent[DIR_NAME] == DIR_DELETED) {
                    write_dir_entry(ent, name_11, attr, start_cluster);
                    if (write_sector(root_dir_sector + sector, sector_buf)) {
                        return -1;
                    }
                    return 0;
                }
            }
        }
        return -1;  /* Root directory full */
    }

    /* Subdirectory (or FAT32 root): cluster chain */
    uint32_t cluster = cur_cluster;
    while (cluster != 0xFFFFFFFFu) {
        uint32_t lba = cluster_to_lba(cluster);
        uint32_t entries_per_cluster =
            sectors_per_cluster * (bytes_per_sector / DIR_ENTRY_SIZE);
        uint32_t e;
        for (e = 0; e < entries_per_cluster; e++) {
            uint32_t sector_in_cluster =
                lba + ((e * DIR_ENTRY_SIZE) / bytes_per_sector);
            if (read_sector(sector_in_cluster, sector_buf)) {
                return -1;
            }
            uint8_t *ent = &sector_buf[(e * DIR_ENTRY_SIZE) % bytes_per_sector];
            if (ent[DIR_NAME] == DIR_FREE ||
                ent[DIR_NAME] == DIR_DELETED) {
                write_dir_entry(ent, name_11, attr, start_cluster);
                if (write_sector(sector_in_cluster, sector_buf)) {
                    return -1;
                }
                return 0;
            }
        }
        cluster = fat_next_cluster(cluster);
    }

    return -1;  /* Directory full */
}

/* Delete a directory entry at `path`. Marks the entry as deleted and frees
 * its cluster chain if it is a directory. FAT12/FAT16 only for now.
 * Returns 0 on success, -1 on error. */
int fat_delete_dir_entry(const char *path) {
    char name_11[11];
    int pos = 0;
    uint32_t cur_cluster = 0;
    int in_dir = 0;

    /* Set starting directory */
    if (is_fat32) {
        uint8_t bs[512];
        if (read_sector(fat_boot_sector, bs)) {
            return -1;
        }
        cur_cluster = read_u32(&bs[FAT32_ROOT_CLUSTER]);
    } else {
        cur_cluster = 0;
    }

    /* Walk all but the last path component to reach the parent directory */
    while (path[pos]) {
        int consumed = parse_component(&path[pos], name_11);
        int next_pos = pos + consumed;

        /* Last component: this is the name to delete */
        if (path[next_pos] == '\0') {
            break;
        }

        uint32_t found_cluster = 0;
        uint32_t found_size = 0;
        uint8_t found_attr = 0;
        int found;

        if (is_fat32 || !in_dir) {
            found = scan_dir_for_name(root_dir_sector, root_dir_entries,
                                      name_11, &found_cluster, &found_size,
                                      &found_attr);
            in_dir = 1;
        } else {
            found = scan_fat32_dir_chain(cur_cluster, name_11,
                                         &found_cluster, &found_size,
                                         &found_attr);
        }

        if (found != 0 || !(found_attr & ATTR_DIRECTORY)) {
            return -1;  /* Parent not found or not a directory */
        }

        cur_cluster = found_cluster;
        pos = next_pos;
    }

    /* Parse final component as the entry name to delete */
    parse_component(&path[pos], name_11);

    /* Find the entry in the parent directory */
    if (!is_fat32 && cur_cluster == 0) {
        /* FAT12/FAT16 root directory: linear region */
        uint32_t sector;
        for (sector = 0; sector < root_dir_size_sectors; sector++) {
            uint32_t e;
            if (read_sector(root_dir_sector + sector, sector_buf)) {
                return -1;
            }
            for (e = 0; e < bytes_per_sector / DIR_ENTRY_SIZE; e++) {
                uint8_t *ent = &sector_buf[e * DIR_ENTRY_SIZE];
                if (ent[DIR_NAME] == DIR_FREE ||
                    ent[DIR_NAME] == DIR_DELETED) {
                    continue;
                }
                int name_match = 1;
                for (uint32_t n = 0; n < 11; n++) {
                    if (ent[n] != (uint8_t)name_11[n]) {
                        name_match = 0;
                        break;
                    }
                }
                if (name_match) {
                    ent[DIR_NAME] = DIR_DELETED;
                    if (write_sector(root_dir_sector + sector, sector_buf)) {
                        return -1;
                    }
                    /* Free cluster chain if it is a directory */
                    if (ent[DIR_ATTR] & ATTR_DIRECTORY) {
                        uint32_t clus = read_u16(&ent[DIR_FST_CLUS_LO]) |
                                        ((uint32_t)read_u16(&ent[DIR_FST_CLUS_HI])
                                         << 16);
                        if (clus >= 2u) {
                            fat_free_cluster_chain(clus);
                        }
                    }
                    return 0;
                }
            }
        }
        return -1;  /* Not found */
    }

    /* Subdirectory (or FAT32 root): cluster chain */
    uint32_t cluster = cur_cluster;
    while (cluster != 0xFFFFFFFFu) {
        uint32_t lba = cluster_to_lba(cluster);
        uint32_t entries_per_cluster =
            sectors_per_cluster * (bytes_per_sector / DIR_ENTRY_SIZE);
        uint32_t e;
        for (e = 0; e < entries_per_cluster; e++) {
            uint32_t sector_in_cluster =
                lba + ((e * DIR_ENTRY_SIZE) / bytes_per_sector);
            if (read_sector(sector_in_cluster, sector_buf)) {
                return -1;
            }
            uint8_t *ent = &sector_buf[(e * DIR_ENTRY_SIZE) % bytes_per_sector];
            if (ent[DIR_NAME] == DIR_FREE ||
                ent[DIR_NAME] == DIR_DELETED) {
                continue;
            }
            int name_match = 1;
            for (uint32_t n = 0; n < 11; n++) {
                if (ent[n] != (uint8_t)name_11[n]) {
                    name_match = 0;
                    break;
                }
            }
            if (name_match) {
                ent[DIR_NAME] = DIR_DELETED;
                if (write_sector(sector_in_cluster, sector_buf)) {
                    return -1;
                }
                /* Free cluster chain if it is a directory */
                if (ent[DIR_ATTR] & ATTR_DIRECTORY) {
                    uint32_t clus = read_u16(&ent[DIR_FST_CLUS_LO]) |
                                    ((uint32_t)read_u16(&ent[DIR_FST_CLUS_HI])
                                     << 16);
                    if (clus >= 2u) {
                        fat_free_cluster_chain(clus);
                    }
                }
                return 0;
            }
        }
        cluster = fat_next_cluster(cluster);
    }

    return -1;  /* Not found */
}

/* Initialize a new directory with `.` and `..` entries.
 * Writes to the directory's first cluster sector. FAT12/FAT16 only.
 * Returns 0 on success, -1 on error. */
int fat_init_dir(uint32_t dir_cluster, uint32_t parent_cluster) {
    uint32_t lba = cluster_to_lba(dir_cluster);

    if (read_sector(lba, sector_buf)) {
        return -1;
    }

    /* `.` entry: name = ".          ", attr = directory, cluster = dir_cluster */
    const char dot_name[11] = {'.', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
                               ' ', ' ', ' '};
    write_dir_entry(&sector_buf[0], dot_name, ATTR_DIRECTORY, dir_cluster);

    /* `..` entry: name = "..         ", attr = directory, cluster = parent */
    const char dotdot_name[11] = {'.', '.', ' ', ' ', ' ', ' ', ' ', ' ',
                                  ' ', ' ', ' '};
    write_dir_entry(&sector_buf[DIR_ENTRY_SIZE], dotdot_name,
                    ATTR_DIRECTORY, parent_cluster);

    if (write_sector(lba, sector_buf)) {
        return -1;
    }

    return 0;
}

/* Look up the starting cluster of a directory. Returns 0 on success,
 * -1 on error. On success, *cluster is set to the directory's cluster
 * (0 for the root directory). FAT12/FAT16 only. */
int fat_get_dir_cluster(const char *path, uint32_t *cluster) {
    char name_11[11];
    int pos = 0;
    uint32_t cur_cluster = 0;
    int in_dir = 0;

    if (path == 0 || path[0] == '\0') {
        *cluster = 0;  /* Root directory */
        return 0;
    }

    /* Set starting directory */
    if (is_fat32) {
        uint8_t bs[512];
        if (read_sector(fat_boot_sector, bs)) {
            return -1;
        }
        cur_cluster = read_u32(&bs[FAT32_ROOT_CLUSTER]);
    } else {
        cur_cluster = 0;
    }

    while (path[pos]) {
        int consumed = parse_component(&path[pos], name_11);
        uint32_t found_cluster = 0;
        uint32_t found_size = 0;
        uint8_t found_attr = 0;
        int found;

        pos += consumed;

        if (is_fat32 || !in_dir) {
            found = scan_dir_for_name(root_dir_sector, root_dir_entries,
                                      name_11, &found_cluster, &found_size,
                                      &found_attr);
            in_dir = 1;
        } else {
            found = scan_fat32_dir_chain(cur_cluster, name_11,
                                         &found_cluster, &found_size,
                                         &found_attr);
        }

        if (found != 0 || !(found_attr & ATTR_DIRECTORY)) {
            return -1;  /* Not found or not a directory */
        }

        cur_cluster = found_cluster;
    }

    *cluster = cur_cluster;
    return 0;
}

/* Write a 32-byte directory entry from an 11-byte padded name. */
static void write_dir_entry(uint8_t *ent, const char *name_11, uint8_t attr,
                            uint32_t start_cluster) {
    uint32_t i;

    /* Name (11 bytes) */
    for (i = 0; i < 11; i++) {
        ent[DIR_NAME + i] = (uint8_t)name_11[i];
    }

    /* Attributes */
    ent[DIR_ATTR] = attr;

    /* Zero reserved/unset fields */
    for (i = 12; i <= 21; i++) {
        ent[i] = 0;
    }
    for (i = 22; i <= 25; i++) {
        ent[i] = 0;
    }

    ent[DIR_FST_CLUS_HI] = (uint8_t)((start_cluster >> 16) & 0xFFu);
    ent[DIR_FST_CLUS_LO] = (uint8_t)(start_cluster & 0xFFu);

    /* File size = 0 */
    ent[DIR_FILE_SIZE + 0] = 0;
    ent[DIR_FILE_SIZE + 1] = 0;
    ent[DIR_FILE_SIZE + 2] = 0;
    ent[DIR_FILE_SIZE + 3] = 0;
}

/* ------------------------------------------------------------------ */
/*  Name helpers                                                      */
/* ------------------------------------------------------------------ */

/* Parse a path component into an 11-char padded, uppercased name.
 * Returns index past this component (skipping leading slashes, including
 * trailing slash if present). */
static int parse_component(const char *path, char *out_name) {
    int i = 0;
    int j = 0;

    while (path[i] == '/') {
        i++;
    }

    /* Copy base name (up to 8 chars, stop at dot, slash, NUL, or j==8) */
    while (path[i] && path[i] != '/' && path[i] != '.' && j < 8) {
        char c = path[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        out_name[j++] = c;
        i++;
    }

    /* Pad base name with spaces */
    while (j < 8) {
        out_name[j++] = ' ';
    }

    /* Skip extension dot */
    if (path[i] == '.') {
        i++;
    }

    /* Copy extension (up to 3 chars) */
    while (path[i] && path[i] != '/' && j < 11) {
        char c = path[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        out_name[j++] = c;
        i++;
    }

    /* Pad extension with spaces */
    while (j < 11) {
        out_name[j++] = ' ';
    }

    /* Return index past this component (skipping trailing slash if present) */
    if (path[i] == '/') {
        i++;
    }
    return i;
}

/* Case-insensitive compare of an 11-char directory name field. */
static int dir_name_match(const uint8_t *entry, const char *name_11) {
    int i;
    for (i = 0; i < 11; i++) {
        char c = name_11[i];
        char e = entry[DIR_NAME + i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (e >= 'a' && e <= 'z') e -= 32;
        if (c != e) {
            return 0;
        }
    }
    return 1;
}

/* Extract starting cluster and file size from a directory entry. */
static void dir_entry_info(const uint8_t *entry, uint32_t *cluster, uint32_t *size) {
    uint16_t hi = read_u16(&entry[DIR_FST_CLUS_HI]);
    uint16_t lo = read_u16(&entry[DIR_FST_CLUS_LO]);
    *cluster = ((uint32_t)hi << 16) | lo;
    *size = read_u32(&entry[DIR_FILE_SIZE]);
}

/* ------------------------------------------------------------------ */
/*  Directory scanning                                                */
/* ------------------------------------------------------------------ */

/* Scan a linear directory region (FAT16 root dir, or a directory's
 * cluster chain for FAT32) for a name. On match, fills in cluster/size
 * and returns 0. Returns -1 if not found. */
static int scan_dir_for_name(uint32_t start_sector, uint32_t total_entries,
                             const char *name_11, uint32_t *found_cluster,
                             uint32_t *found_size, uint8_t *found_attr) {
    uint32_t entries_per_sector = bytes_per_sector / DIR_ENTRY_SIZE;
    uint32_t sector;

    for (sector = 0; sector < (total_entries + entries_per_sector - 1) / entries_per_sector; sector++) {
        if (read_sector(start_sector + sector, sector_buf)) {
            return -1;
        }
        uint32_t e;
        for (e = 0; e < entries_per_sector; e++) {
            const uint8_t *ent = &sector_buf[e * DIR_ENTRY_SIZE];
            if (ent[DIR_NAME] == DIR_FREE) {
                return -1;  /* end of directory */
            }
            if (ent[DIR_NAME] == DIR_DELETED) {
                continue;
            }
            if (ent[DIR_ATTR] == ATTR_LFN) {
                continue;
            }
            if (dir_name_match(ent, name_11)) {
                dir_entry_info(ent, found_cluster, found_size);
                if (found_attr) {
                    *found_attr = ent[DIR_ATTR];
                }
                return 0;
            }
        }
    }
    return -1;
}

/* Scan a FAT32 directory cluster chain for a name. */
static int scan_fat32_dir_chain(uint32_t start_cluster, const char *name_11,
                                uint32_t *found_cluster, uint32_t *found_size,
                                uint8_t *found_attr) {
    uint32_t cluster = start_cluster;
    uint32_t entries_per_cluster = sectors_per_cluster *
                                   (bytes_per_sector / DIR_ENTRY_SIZE);

    while (cluster != 0xFFFFFFFFu) {
        uint32_t lba = cluster_to_lba(cluster);
        uint32_t e;
        for (e = 0; e < entries_per_cluster; e++) {
            uint32_t sector_in_cluster = lba + ((e * DIR_ENTRY_SIZE) / bytes_per_sector);
            if (read_sector(sector_in_cluster, sector_buf)) {
                return -1;
            }
            const uint8_t *ent = &sector_buf[(e * DIR_ENTRY_SIZE) % bytes_per_sector];
            if (ent[DIR_NAME] == DIR_FREE) {
                return -1;
            }
            if (ent[DIR_NAME] == DIR_DELETED) {
                continue;
            }
            if (ent[DIR_ATTR] == ATTR_LFN) {
                continue;
            }
            if (dir_name_match(ent, name_11)) {
                dir_entry_info(ent, found_cluster, found_size);
                if (found_attr) {
                    *found_attr = ent[DIR_ATTR];
                }
                return 0;
            }
        }
        cluster = fat_next_cluster(cluster);
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

int fat_init(void) {
    uint8_t bs[512];

    if (read_sector(fat_boot_sector, bs)) {
        serial_puts("fat: failed to read boot sector\n");
        return -1;
    }

    if (bs[510] != 0x55 || bs[511] != 0xAA) {
        serial_puts("fat: no boot signature\n");
        return -1;
    }

    bytes_per_sector = read_u16(&bs[FAT_BYTES_PER_SECTOR]);
    sectors_per_cluster = bs[FAT_SECTORS_PER_CLUSTER];
    reserved_sector_count = read_u16(&bs[FAT_RESERVED_SECTORS]);
    num_fats = bs[FAT_NUM_FATS];
    root_dir_entries = read_u16(&bs[FAT_ROOT_ENTRIES]);
    sectors_per_fat = read_u16(&bs[FAT_SECTORS_PER_FAT16]);
    if (sectors_per_fat == 0) {
        sectors_per_fat = read_u32(&bs[FAT32_SECTORS_PER_FAT]);
    }

    fat_start_sector = fat_boot_sector + reserved_sector_count;

    /*
     * The FAT type is determined by the number of data clusters, not by
     * the presence of the FAT32 extended fields. Small volumes are FAT12.
     */
    {
        uint32_t total_sectors = read_u16(&bs[FAT_TOTAL_SECTORS16]);
        uint32_t root_dir_sectors;
        uint32_t data_sectors;
        uint32_t clusters;

        if (total_sectors == 0) {
            total_sectors = read_u32(&bs[FAT_TOTAL_SECTORS32]);
        }

        root_dir_sectors = ((uint32_t)root_dir_entries * DIR_ENTRY_SIZE +
                            bytes_per_sector - 1) / bytes_per_sector;
        data_sectors = total_sectors - reserved_sector_count -
                       ((uint32_t)num_fats * sectors_per_fat) -
                       root_dir_sectors;
        clusters = data_sectors / sectors_per_cluster;

        is_fat12 = 0;
        is_fat32 = 0;

        if (clusters < 4085u) {
            is_fat12 = 1;
        } else if (clusters >= 65525u) {
            is_fat32 = 1;
        }
    }

    if (is_fat32) {
        uint32_t root_cluster = read_u32(&bs[FAT32_ROOT_CLUSTER]);
        data_start_sector = fat_start_sector + (num_fats * sectors_per_fat);
        root_dir_sector = cluster_to_lba(root_cluster);
        serial_puts("fat: FAT32 detected\n");
    } else {
        uint32_t root_dir_sectors = ((root_dir_entries * DIR_ENTRY_SIZE) +
                                     bytes_per_sector - 1) / bytes_per_sector;
        data_start_sector = fat_start_sector + (num_fats * sectors_per_fat) +
                            root_dir_sectors;
        root_dir_sector = fat_start_sector + (num_fats * sectors_per_fat);
        root_dir_size_sectors = root_dir_sectors;
        if (is_fat12) {
            serial_puts("fat: FAT12 detected\n");
        } else {
            serial_puts("fat: FAT16 detected\n");
        }
    }

    cluster_size_bytes = (uint32_t)sectors_per_cluster * bytes_per_sector;

    serial_puts("fat: initialized\n");
    return 0;
}

int fat_open(const char *path) {
    char name_11[11];
    int pos = 0;
    uint32_t cur_cluster = 0;
    uint32_t cur_size = 0;
    int in_dir = 0;

    if (file_open) {
        serial_puts("fat: file already open\n");
        return -1;
    }

    /* Set starting directory */
    if (is_fat32) {
        uint8_t bs[512];
        if (read_sector(fat_boot_sector, bs)) {
            return -1;
        }
        cur_cluster = read_u32(&bs[FAT32_ROOT_CLUSTER]);
    } else {
        cur_cluster = 0;  /* FAT16 root dir uses sector, not cluster */
    }

    while (path[pos]) {
        int consumed = parse_component(&path[pos], name_11);
        pos += consumed;

        uint32_t found_cluster = 0;
        uint32_t found_size = 0;
        int found = -1;

        if (is_fat32) {
            found = scan_fat32_dir_chain(cur_cluster, name_11,
                                         &found_cluster, &found_size, 0);
        } else {
            if (!in_dir) {
                /* FAT16 root directory is linear, not a cluster chain */
                found = scan_dir_for_name(root_dir_sector, root_dir_entries,
                                          name_11, &found_cluster, &found_size,
                                          0);
                in_dir = 1;
            } else {
                /* Subdirectory in FAT16 - treat as cluster chain */
                found = scan_fat32_dir_chain(cur_cluster, name_11,
                                             &found_cluster, &found_size, 0);
            }
        }

        if (found != 0) {
            serial_puts("fat: path component not found\n");
            return -1;
        }

        cur_cluster = found_cluster;
        cur_size = found_size;

        /* If there are more path components, this entry must be a directory */
        if (path[pos]) {
            /* Check if it's a directory */
            /* We need to re-read the entry to check attributes */
            /* For now, assume directories work */
        }
    }

    current_cluster = cur_cluster;
    current_offset = 0;
    current_size = cur_size;
    file_open = 1;

    serial_puts("fat: file opened\n");
    return 0;
}

int32_t fat_read(void *buf, uint32_t size) {
    if (!file_open) {
        return -1;
    }

    uint8_t *dst = (uint8_t *)buf;
    uint32_t bytes_read = 0;
    uint32_t cluster = current_cluster;
    uint32_t offset = current_offset;
    uint32_t remaining = size;

    if (offset >= current_size) {
        return 0;  /* EOF */
    }

    if (remaining > current_size - offset) {
        remaining = current_size - offset;
    }

    while (remaining > 0 && cluster != 0xFFFFFFFFu) {
        uint32_t lba = cluster_to_lba(cluster);
        uint32_t cluster_offset = offset % cluster_size_bytes;
        uint32_t sector_in_cluster = cluster_offset / bytes_per_sector;
        uint32_t sector_offset_in_sector = cluster_offset % bytes_per_sector;

        /* Read the sector containing our data */
        if (read_sector(lba + sector_in_cluster, sector_buf)) {
            return (bytes_read > 0) ? (int32_t)bytes_read : -1;
        }

        uint32_t avail_in_sector = bytes_per_sector - sector_offset_in_sector;
        if (avail_in_sector > remaining) {
            avail_in_sector = remaining;
        }

        /* Copy from sector buffer to destination */
        uint32_t i;
        for (i = 0; i < avail_in_sector; i++) {
            dst[bytes_read + i] = sector_buf[sector_offset_in_sector + i];
        }
        bytes_read += avail_in_sector;
        remaining -= avail_in_sector;
        offset += avail_in_sector;

        /* Move to next sector/cluster if needed */
        if ((offset % cluster_size_bytes) == 0) {
            cluster = fat_next_cluster(cluster);
        }
    }

    current_offset = offset;
    return (int32_t)bytes_read;
}

int32_t fat_size(void) {
    if (!file_open) {
        return -1;
    }
    return (int32_t)current_size;
}

void fat_close(void) {
    file_open = 0;
    current_cluster = 0;
    current_offset = 0;
    current_size = 0;
}

int fat_is_open(void) {
    return file_open;
}

int fat_is_dir(const char *path) {
    char name_11[11];
    int pos = 0;
    int first = 1;
    uint32_t cur_cluster = 0;

    if (path == 0 || path[0] == '\0') {
        return 1;  /* the root directory always exists */
    }

    if (is_fat32) {
        uint8_t bs[512];
        if (read_sector(fat_boot_sector, bs)) {
            return 0;
        }
        cur_cluster = read_u32(&bs[FAT32_ROOT_CLUSTER]);
    }

    while (path[pos]) {
        int consumed = parse_component(&path[pos], name_11);
        uint32_t found_cluster = 0;
        uint32_t found_size = 0;
        uint8_t found_attr = 0;
        int found;

        pos += consumed;

        if (is_fat32 || !first) {
            found = scan_fat32_dir_chain(cur_cluster, name_11,
                                         &found_cluster, &found_size,
                                         &found_attr);
        } else {
            found = scan_dir_for_name(root_dir_sector, root_dir_entries,
                                      name_11, &found_cluster, &found_size,
                                      &found_attr);
        }

        if (found != 0 || !(found_attr & ATTR_DIRECTORY)) {
            return 0;
        }

        cur_cluster = found_cluster;
        first = 0;
    }

    return 1;
}

static void print_dir_entry(const uint8_t *ent, int long_format) {
    char disp[13];
    int j = 0;
    int k;

    /* Format display name from the 11-char padded entry. */
    for (k = 0; k < 8; k++) {
        if (ent[DIR_NAME + k] != ' ') {
            disp[j++] = ent[DIR_NAME + k];
        }
    }

    int has_ext = 0;
    for (k = 8; k < 11; k++) {
        if (ent[DIR_NAME + k] != ' ') {
            has_ext = 1;
            break;
        }
    }
    if (has_ext) {
        disp[j++] = '.';
        for (k = 8; k < 11; k++) {
            if (ent[DIR_NAME + k] != ' ') {
                disp[j++] = ent[DIR_NAME + k];
            }
        }
    }
    disp[j] = '\0';

    if (long_format) {
        if (ent[DIR_ATTR] & ATTR_DIRECTORY) {
            console_puts("        <DIR> ");
        } else {
            uint32_t sz = read_u32(&ent[DIR_FILE_SIZE]);
            uint32_t tmp = sz;
            int digits = 1;
            while (tmp >= 10) {
                tmp /= 10;
                digits++;
            }
            for (int s = digits; s < 12; s++) {
                console_putc(' ');
            }
            console_put_dec(sz);
            console_putc(' ');
        }
        console_puts(disp);
        console_putc('\n');
    } else {
        console_puts(disp);
        if (ent[DIR_ATTR] & ATTR_DIRECTORY) {
            console_puts("  <DIR>\n");
        } else {
            console_putc('\n');
        }
    }
}

int fat_list_dir(const char *path, int long_format) {
    char name_11[11];
    int pos = 0;
    uint32_t cur_cluster = 0;

    /* Set starting directory */
    if (is_fat32) {
        uint8_t bs[512];
        if (read_sector(fat_boot_sector, bs)) {
            serial_puts("fat: failed to read boot sector\n");
            return -1;
        }
        cur_cluster = read_u32(&bs[FAT32_ROOT_CLUSTER]);
    } else {
        cur_cluster = 0;  /* FAT16 root dir uses sector, not cluster */
    }

    /* Walk path components to reach the target directory */
    while (path[pos]) {
        int consumed = parse_component(&path[pos], name_11);
        pos += consumed;

        uint32_t found_cluster = 0;
        uint32_t found_size = 0;
        int found = -1;

        if (is_fat32 || cur_cluster != 0) {
            found = scan_fat32_dir_chain(cur_cluster, name_11,
                                         &found_cluster, &found_size, 0);
        } else {
            found = scan_dir_for_name(root_dir_sector, root_dir_entries,
                                      name_11, &found_cluster, &found_size, 0);
        }

        if (found != 0) {
            serial_puts("fat: directory not found\n");
            return -1;
        }

        cur_cluster = found_cluster;
    }

    /* FAT12/FAT16 root directories are a fixed linear region, not a cluster
     * chain (cur_cluster == 0 marks that case). */
    if (!is_fat32 && cur_cluster == 0) {
        uint32_t sector;

        for (sector = 0; sector < root_dir_size_sectors; sector++) {
            uint32_t e;

            if (read_sector(root_dir_sector + sector, sector_buf)) {
                return -1;
            }

            for (e = 0; e < bytes_per_sector / DIR_ENTRY_SIZE; e++) {
                const uint8_t *ent = &sector_buf[e * DIR_ENTRY_SIZE];

                if (ent[DIR_NAME] == DIR_FREE) {
                    return 0;
                }
                if (ent[DIR_NAME] == DIR_DELETED) {
                    continue;
                }
                if (ent[DIR_ATTR] == ATTR_LFN) {
                    continue;
                }

                print_dir_entry(ent, long_format);
            }
        }
        return 0;
    }

    /* List all entries in the directory */
    uint32_t cluster = cur_cluster;
    uint32_t entries_per_cluster = sectors_per_cluster *
                                   (bytes_per_sector / DIR_ENTRY_SIZE);

    while (cluster != 0xFFFFFFFFu) {
        uint32_t lba = cluster_to_lba(cluster);
        uint32_t e;
        for (e = 0; e < entries_per_cluster; e++) {
            uint32_t sector_in_cluster = lba + ((e * DIR_ENTRY_SIZE) / bytes_per_sector);
            if (read_sector(sector_in_cluster, sector_buf)) {
                return -1;
            }
            const uint8_t *ent = &sector_buf[(e * DIR_ENTRY_SIZE) % bytes_per_sector];

            if (ent[DIR_NAME] == DIR_FREE) {
                return 0;  /* end of directory */
            }
            if (ent[DIR_NAME] == DIR_DELETED) {
                continue;
            }
            if (ent[DIR_ATTR] == ATTR_LFN) {
                continue;
            }

            print_dir_entry(ent, long_format);
        }
        cluster = fat_next_cluster(cluster);
    }

    return 0;
}

void fat_set_boot_sector(uint32_t lba) {
    fat_boot_sector = lba;
}

void fat_set_memdisk(void *base, uint32_t size) {
    memdisk_base = (uint8_t *)base;
    memdisk_sectors = size / 512u;
}
