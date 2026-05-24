#include "elf.h"
#include "paging.h"
#include "string.h"

static int elf_header_valid(const elf32_ehdr_t *eh, uint32_t len)
{
    if (!eh || len < sizeof(elf32_ehdr_t)) {
        return 0;
    }

    if (eh->e_ident[0] != ELF_MAGIC0 ||
        eh->e_ident[1] != ELF_MAGIC1 ||
        eh->e_ident[2] != ELF_MAGIC2 ||
        eh->e_ident[3] != ELF_MAGIC3) {
        return 0;
    }

    if (eh->e_ident[4] != ELF_CLASS32  ||
        eh->e_ident[5] != ELF_DATA2LSB ||
        eh->e_type      != ELF_ET_EXEC  ||
        eh->e_machine   != ELF_EM_386) {
        return 0;
    }

    if (eh->e_ehsize != sizeof(elf32_ehdr_t) ||
        eh->e_phentsize != sizeof(elf32_phdr_t) ||
        eh->e_phoff == 0u || eh->e_phnum == 0u) {
        return 0;
    }

    uint32_t ph_bytes = (uint32_t)eh->e_phnum * eh->e_phentsize;
    if (ph_bytes / eh->e_phentsize != (uint32_t)eh->e_phnum) {
        return 0;
    }
    if (eh->e_phoff > len || ph_bytes > len - eh->e_phoff) {
        return 0;
    }

    return 1;
}

static int elf_segment_in_file(const elf32_phdr_t *ph, uint32_t len)
{
    if (ph->p_memsz < ph->p_filesz) {
        return 0;
    }
    if (ph->p_offset > len || ph->p_filesz > len - ph->p_offset) {
        return 0;
    }
    return 1;
}

static int user_range_valid(uint32_t start, uint32_t size)
{
    if (size == 0u || start < USER_LOAD_BASE || start >= USER_STACK_BASE) {
        return 0;
    }

    uint32_t end = start + size;
    if (end < start || end > USER_STACK_BASE) {
        return 0;
    }

    return 1;
}

/* -------------------------------------------------------------------------
 * elf_load – load an ELF32 executable from a memory buffer
 * ---------------------------------------------------------------------- */
uint32_t elf_load(const void *data, uint32_t len)
{
    if (!data) {
        return 0;
    }

    const elf32_ehdr_t *eh = (const elf32_ehdr_t *)data;
    if (!elf_header_valid(eh, len)) {
        return 0;
    }

    const uint8_t *base = (const uint8_t *)data;

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const elf32_phdr_t *ph =
            (const elf32_phdr_t *)(base + eh->e_phoff +
                                   (uint32_t)i * eh->e_phentsize);

        if (ph->p_type != ELF_PT_LOAD) {
            continue;
        }
        if (!elf_segment_in_file(ph, len)) {
            return 0;
        }

        if (ph->p_filesz > 0u) {
            memcpy((void *)ph->p_vaddr,
                   base + ph->p_offset,
                   ph->p_filesz);
        }

        if (ph->p_memsz > ph->p_filesz) {
            memset((void *)(ph->p_vaddr + ph->p_filesz),
                   0,
                   ph->p_memsz - ph->p_filesz);
        }
    }

    return eh->e_entry;
}

/* -------------------------------------------------------------------------
 * elf_load_user – load ELF segments into a user-mode page directory
 * ---------------------------------------------------------------------- */
/* Look up the physical frame address for a virtual page in a given PD.
 * The PD and its page tables are in the identity-mapped 0-8 MB region,
 * so we can access them directly by physical address.
 * Returns the physical address of the frame, or 0 if not mapped.
 */
static uint32_t phys_of_virt(uint32_t pd_phys, uint32_t virt)
{
    const uint32_t *pd  = (const uint32_t *)pd_phys;
    uint32_t        pdi = virt >> 22u;
    uint32_t        pti = (virt >> 12u) & 0x3FFu;

    if (!(pd[pdi] & PAGE_PRESENT)) {
        return 0u;
    }
    const uint32_t *pt = (const uint32_t *)(pd[pdi] & ~0xFFFu);
    if (!(pt[pti] & PAGE_PRESENT)) {
        return 0u;
    }
    return pt[pti] & ~0xFFFu;
}

uint32_t elf_load_user(const void *data, uint32_t len, uint32_t pd_phys)
{
    if (!data || !pd_phys) {
        return 0u;
    }

    const elf32_ehdr_t *eh = (const elf32_ehdr_t *)data;
    if (!elf_header_valid(eh, len) ||
        eh->e_entry < USER_LOAD_BASE || eh->e_entry >= USER_STACK_BASE) {
        return 0u;
    }

    const uint8_t *base = (const uint8_t *)data;
    int entry_covered = 0;

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const elf32_phdr_t *ph =
            (const elf32_phdr_t *)(base + eh->e_phoff +
                                   (uint32_t)i * eh->e_phentsize);

        if (ph->p_type != ELF_PT_LOAD || ph->p_memsz == 0u) {
            continue;
        }
        if (!elf_segment_in_file(ph, len) ||
            !user_range_valid(ph->p_vaddr, ph->p_memsz)) {
            return 0u;
        }

        if (eh->e_entry >= ph->p_vaddr &&
            eh->e_entry < ph->p_vaddr + ph->p_memsz) {
            entry_covered = 1;
        }

        /* Page-align the virtual range */
        uint32_t page_start = ph->p_vaddr & ~0xFFFu;
        uint32_t page_end   = (ph->p_vaddr + ph->p_memsz + 0xFFFu) & ~0xFFFu;
        uint32_t n_pages    = (page_end - page_start) >> 12u;

        /* Allocate frames and map them in pd_phys with PAGE_USER */
        if (paging_alloc_user_pages(pd_phys, page_start, n_pages) != 0) {
            return 0u;
        }

        /* Copy or zero-fill each page via the physical frame address */
        for (uint32_t pg = 0u; pg < n_pages; pg++) {
            uint32_t virt_page = page_start + pg * 4096u;
            uint32_t phys_page = phys_of_virt(pd_phys, virt_page);
            if (!phys_page) {
                return 0u;
            }

            /*
             * phys_page is a PMM frame (always in 0–8 MB) which is
             * identity-mapped in the kernel PD (phys == virt).
             * Writing to phys_page here writes to the physical frame that
             * will be mapped at virt_page in the user PD.
             */
            uint8_t *dst = (uint8_t *)phys_page;
            memset(dst, 0, 4096u);

            /* Determine the overlap of this page with the file-data region */
            uint32_t file_start = ph->p_vaddr;
            uint32_t file_end   = ph->p_vaddr + ph->p_filesz;
            uint32_t pg_end     = virt_page + 4096u;

            if (virt_page >= file_end || pg_end <= file_start) {
                continue;   /* page is entirely BSS – already zeroed */
            }

            uint32_t copy_vstart = (virt_page > file_start)
                                   ? virt_page : file_start;
            uint32_t copy_vend   = (pg_end < file_end) ? pg_end : file_end;

            uint32_t dst_off = copy_vstart - virt_page;
            uint32_t src_off = ph->p_offset + (copy_vstart - ph->p_vaddr);
            uint32_t copy_len = copy_vend - copy_vstart;

            memcpy(dst + dst_off, base + src_off, copy_len);
        }
    }

    if (!entry_covered) {
        return 0u;
    }

    return eh->e_entry;
}
