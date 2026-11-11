#ifndef PAGING_H
#define PAGING_H

#include "types32.h"
#include "boot_info.h"

#define PAGE_DIR_ADDR 0x20000u
#define PAGE_TABLE_ADDR 0x21000u
#define PAGE_SIZE 4096u

void paging_init(const struct BootInfo *bi);

#endif
