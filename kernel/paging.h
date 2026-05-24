#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

/*
 * VESPER OS – Paging (x86 32-bit, two-level page tables)
 *
 * paging_init() sets up an identity-mapped page directory that covers the
 * first 8 MB of physical RAM (two 4 MB page tables), then enables paging
 * by loading CR3 and setting CR0.PG.
 *
 * Per-process page directories (Tier 2):
 *   Each user process gets its own PD allocated from the PMM.  The kernel
 *   PDEs (covering the first 8 MB) are copied into every new PD so that the
 *   kernel is reachable regardless of which PD is loaded.
 *   Kernel threads keep pd_phys == 0 and always run under the kernel PD.
 */

/* Page-table entry flags (bits 0-2) */
#define PAGE_PRESENT   (1u << 0)
#define PAGE_WRITABLE  (1u << 1)
#define PAGE_USER      (1u << 2)   /* accessible from ring 3 */

/*
 * Initialise paging:
 *   – Identity-map physical 0x00000000 – 0x007FFFFF (8 MB)
 *   – Load CR3, enable CR0.PG
 */
void paging_init(void);

/*
 * Map a single 4 KB virtual page to a physical frame.
 * Silently ignored if the PDE for that virtual address has no page table.
 * @flags : combination of PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER
 */
void paging_map_page(uint32_t virt, uint32_t phys, uint32_t flags);

/* Invalidate a single TLB entry */
void paging_flush_tlb(uint32_t virt);

/* Load a new page directory into CR3 (full TLB flush) */
void paging_switch(uint32_t pd_phys);

/* Return the physical address of the current page directory (CR3) */
uint32_t paging_get_cr3(void);

/* Return the physical address of the kernel page directory */
uint32_t paging_get_kernel_pd_phys(void);

/*
 * paging_create_pd – allocate a fresh page directory and copy the kernel
 *                    PDEs so that the kernel remains accessible.
 * Returns the physical address of the new PD, or 0 on allocation failure.
 */
uint32_t paging_create_pd(void);

/*
 * paging_alloc_user_pages – map @n_pages consecutive 4 KB pages in @pd_phys
 *                           starting at virtual address @virt_base.
 * Pages and page tables are allocated from the PMM.
 * Returns 0 on success, -1 on allocation failure.
 */
int paging_alloc_user_pages(uint32_t pd_phys, uint32_t virt_base,
                             uint32_t n_pages);

/*
 * paging_destroy_pd – free all user-mode page tables and physical frames
 *                     mapped in @pd_phys (PDEs 2–1023), then free the PD
 *                     frame itself.
 * Kernel PDEs (0–1) are NOT freed because they are shared with the kernel PD.
 */
void paging_destroy_pd(uint32_t pd_phys);

/*
 * paging_user_range_accessible – verify that every page touched by the user
 * range [virt, virt+len) is mapped in @pd_phys with PAGE_USER set.
 * When @write_required is non-zero, PAGE_WRITABLE must also be set.
 * Returns 1 when the full range is accessible, 0 otherwise.
 */
int paging_user_range_accessible(uint32_t pd_phys, uint32_t virt, uint32_t len,
                                 int write_required);

#endif /* PAGING_H */
