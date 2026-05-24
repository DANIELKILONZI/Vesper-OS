#include "paging.h"
#include "pmm.h"
#include "string.h"

/* -------------------------------------------------------------------------
 * Static paging structures
 *
 * These must be 4 KB-aligned.  Placing them in BSS (zero-initialised by
 * kernel_entry.asm before kernel_main is called) is safe.
 *
 * Two page tables cover the first 8 MB:
 *   page_tables[0] : 0x00000000 – 0x003FFFFF  (4 MB)
 *   page_tables[1] : 0x00400000 – 0x007FFFFF  (4 MB)
 * ---------------------------------------------------------------------- */
#define PT_ENTRIES  1024u
#define PD_ENTRIES  1024u
#define PAGE_SIZE   4096u

static uint32_t page_directory[PD_ENTRIES]     __attribute__((aligned(4096)));
static uint32_t page_tables[2][PT_ENTRIES]     __attribute__((aligned(4096)));

static int paging_page_accessible(uint32_t pd_phys, uint32_t virt,
                                  int write_required)
{
    if (!pd_phys) {
        return 0;
    }

    const uint32_t *pd = (const uint32_t *)pd_phys;
    uint32_t pdi = virt >> 22u;
    uint32_t pti = (virt >> 12u) & 0x3FFu;

    if (!(pd[pdi] & PAGE_PRESENT) || !(pd[pdi] & PAGE_USER)) {
        return 0;
    }

    const uint32_t *pt = (const uint32_t *)(pd[pdi] & ~0xFFFu);
    uint32_t pte = pt[pti];
    if (!(pte & PAGE_PRESENT) || !(pte & PAGE_USER)) {
        return 0;
    }
    if (write_required && !(pte & PAGE_WRITABLE)) {
        return 0;
    }

    return 1;
}

void paging_init(void)
{
    /* Clear page directory (all entries not-present) */
    memset(page_directory, 0, sizeof(page_directory));

    /* Build identity-map page tables for the first 8 MB */
    for (uint32_t t = 0; t < 2u; t++) {
        for (uint32_t i = 0; i < PT_ENTRIES; i++) {
            uint32_t phys = t * (4u * 1024u * 1024u) + i * PAGE_SIZE;
            page_tables[t][i] = phys | PAGE_PRESENT | PAGE_WRITABLE;
        }
        /* Point the PDE at our static page table */
        page_directory[t] = (uint32_t)page_tables[t] | PAGE_PRESENT | PAGE_WRITABLE;
    }

    /* Load CR3 with the physical address of the page directory.
     * (virtual == physical here because paging is not yet enabled) */
    __asm__ volatile ("mov %0, %%cr3" : : "r"(page_directory) : "memory");

    /* Enable paging: set CR0.PG (bit 31) */
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

void paging_map_page(uint32_t virt, uint32_t phys, uint32_t flags)
{
    uint32_t pdi = virt >> 22u;
    uint32_t pti = (virt >> 12u) & 0x3FFu;

    if (!(page_directory[pdi] & PAGE_PRESENT)) {
        return;   /* no page table allocated for this 4 MB region */
    }

    uint32_t *pt = (uint32_t *)(page_directory[pdi] & ~0xFFFu);
    pt[pti] = (phys & ~0xFFFu) | PAGE_PRESENT | (flags & 0xFFEu);
    paging_flush_tlb(virt);
}

void paging_flush_tlb(uint32_t virt)
{
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

void paging_switch(uint32_t pd_phys)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pd_phys) : "memory");
}

uint32_t paging_get_cr3(void)
{
    uint32_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

uint32_t paging_get_kernel_pd_phys(void)
{
    /* Identity-mapped: virtual address == physical address */
    return (uint32_t)page_directory;
}

/* -------------------------------------------------------------------------
 * paging_create_pd – allocate a new page directory and copy kernel PDEs
 * ---------------------------------------------------------------------- */
uint32_t paging_create_pd(void)
{
    uint32_t pd_phys = pmm_alloc_frame();
    if (!pd_phys) {
        return 0u;
    }

    uint32_t *new_pd = (uint32_t *)pd_phys;   /* identity-mapped */
    memset(new_pd, 0, PAGE_SIZE);

    /* Copy kernel PDEs (first 8 MB = PDEs 0 and 1) into the new PD */
    new_pd[0] = page_directory[0];
    new_pd[1] = page_directory[1];

    return pd_phys;
}

/* -------------------------------------------------------------------------
 * paging_alloc_user_pages – map n_pages at virt_base in a given PD
 * ---------------------------------------------------------------------- */
int paging_alloc_user_pages(uint32_t pd_phys, uint32_t virt_base,
                             uint32_t n_pages)
{
    uint32_t *pd = (uint32_t *)pd_phys;

    for (uint32_t i = 0; i < n_pages; i++) {
        uint32_t virt = virt_base + i * PAGE_SIZE;
        uint32_t pdi  = virt >> 22u;
        uint32_t pti  = (virt >> 12u) & 0x3FFu;

        /* Allocate a page table if this PDE is not yet present */
        if (!(pd[pdi] & PAGE_PRESENT)) {
            uint32_t pt_phys = pmm_alloc_frame();
            if (!pt_phys) {
                return -1;
            }
            memset((void *)pt_phys, 0, PAGE_SIZE);
            pd[pdi] = pt_phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        }

        /* Allocate a physical frame for this virtual page */
        uint32_t frame_phys = pmm_alloc_frame();
        if (!frame_phys) {
            return -1;
        }

        uint32_t *pt = (uint32_t *)(pd[pdi] & ~0xFFFu);
        pt[pti] = frame_phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * paging_destroy_pd – free all user-mode pages/tables, then the PD itself
 * ---------------------------------------------------------------------- */
void paging_destroy_pd(uint32_t pd_phys)
{
    uint32_t *pd = (uint32_t *)pd_phys;

    /* Skip PDEs 0-1 (kernel identity map – shared with kernel PD) */
    for (uint32_t i = 2u; i < PD_ENTRIES; i++) {
        if (!(pd[i] & PAGE_PRESENT)) {
            continue;
        }

        uint32_t  pt_phys = pd[i] & ~0xFFFu;
        uint32_t *pt      = (uint32_t *)pt_phys;

        /* Free each mapped frame */
        for (uint32_t j = 0u; j < PT_ENTRIES; j++) {
            if (pt[j] & PAGE_PRESENT) {
                pmm_free_frame(pt[j] & ~0xFFFu);
            }
        }
        /* Free the page table itself */
        pmm_free_frame(pt_phys);
    }

    /* Free the page directory frame */
    pmm_free_frame(pd_phys);
}

int paging_user_range_accessible(uint32_t pd_phys, uint32_t virt, uint32_t len,
                                 int write_required)
{
    if (len == 0u) {
        return 1;
    }

    uint32_t end = virt + len - 1u;
    if (end < virt) {
        return 0;
    }

    uint32_t page = virt & ~0xFFFu;
    uint32_t end_page = end & ~0xFFFu;

    while (1) {
        if (!paging_page_accessible(pd_phys, page, write_required)) {
            return 0;
        }
        if (page == end_page) {
            break;
        }
        page += PAGE_SIZE;
    }

    return 1;
}
