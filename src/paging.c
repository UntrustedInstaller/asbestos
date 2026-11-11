#include "paging.h"
#include "io.h"
#include "boot_info.h"

void paging_init(const struct BootInfo *bi) {
    uint32_t *page_dir = (uint32_t *)PAGE_DIR_ADDR;
    uint32_t *page_tab = (uint32_t *)PAGE_TABLE_ADDR;

    /* Zero page directory (1024 entries * 4 bytes per entry = 4 KB) */
    for (int i = 0; i < 1024; i++) {
        page_dir[i] = 0;
    }

    /* Zero first page table */
    for (int i = 0; i < 1024; i++) {
        page_tab[i] = 0;
    }

    /* Page directory entry 0 -> page table 0, present, rw, supervisor */
    page_dir[0] = PAGE_TABLE_ADDR | 0x7;

    /* Identity map first 4 MB: 1024 entries covering 0x000000-0x3FFFFF */
    for (uint32_t i = 0; i < 1024; i++) {
        page_tab[i] = (i * PAGE_SIZE) | 0x07;
    }

    /* Map the VBE linear framebuffer page(s) */
    uint32_t fb_addr = bi->fb_addr;
    uint32_t fb_page = fb_addr & ~0xFFF;          /* page-aligned base */
    uint32_t pd_index = fb_page >> 22;            /* bits 31-22 */
    uint32_t pt_index = (fb_page >> 12) & 0x3FF;  /* bits 21-12 */

    if (pd_index == 0) {
        /* Framebuffer is within the first 4 MB, already covered by page table 0 */
        page_tab[pt_index] = fb_page | 0x7;
    } else {
        /* Use a second page table at 0x22000 and map the full 4 MB region */
        uint32_t *page_tab2 = (uint32_t *)0x22000;
        uint32_t region_base = fb_page & ~0x3FFFFF; /* 4 MB aligned base */
        for (int i = 0; i < 1024; i++) {
            page_tab2[i] = (region_base + i * PAGE_SIZE) | 0x7;
        }
        page_dir[pd_index] = 0x22000 | 0x7;
    }

    /* Load CR3 with physical address of the page directory */
    write_cr3(PAGE_DIR_ADDR);

    /* Enable paging by setting bit 31 of CR0 */
    uint32_t cr0 = read_cr0();
    write_cr0(cr0 | (1u << 31));
}
