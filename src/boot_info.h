#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include "types32.h"

/*
 * BootInfo is written by src/stage2.asm at physical address 0x5000 and
 * read by the 32-bit kernel after the protected-mode transition. Keep the
 * layout in sync with the offsets used in stage2.asm.
 */
#define BOOT_INFO_ADDR  0x00005000u
#define BOOT_INFO_MAGIC 0x33425341u   /* "ASB3" */

struct BootInfo {
    uint32_t magic;          /* 0x00  BOOT_INFO_MAGIC */
    uint32_t fb_addr;        /* 0x04  physical linear framebuffer address */
    uint32_t pitch;          /* 0x08  bytes per scanline */
    uint16_t width;          /* 0x0C  pixels */
    uint16_t height;         /* 0x0E  pixels */
    uint8_t  bpp;            /* 0x10  bits per pixel (32 for milestone 1) */
    uint8_t  red_size;       /* 0x11  red channel width */
    uint8_t  red_pos;        /* 0x12  red channel bit position */
    uint8_t  green_size;     /* 0x13  green channel width */
    uint8_t  green_pos;      /* 0x14  green channel bit position */
    uint8_t  blue_size;      /* 0x15  blue channel width */
    uint8_t  blue_pos;       /* 0x16  blue channel bit position */
    uint8_t  boot_drive;     /* 0x17  BIOS drive number */
    uint16_t mode;           /* 0x18  selected VBE mode number */
    uint16_t reserved;       /* 0x1A  must be zero */
    uint32_t reserved2;      /* 0x1C  must be zero */
} __attribute__((packed));

#endif
