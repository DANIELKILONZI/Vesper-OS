#ifndef ELF_H
#define ELF_H

#include <stdint.h>

/*
 * VESPER OS – Minimal ELF32 loader
 *
 * Loads a statically linked ELF32 binary from a byte buffer into memory
 * and returns its entry-point address.  Only PT_LOAD segments are
 * processed; everything else is silently skipped.
 *
 * The caller is responsible for ensuring the target virtual-address ranges
 * are mapped and writable before calling elf_load().
 */

/* ELF32 identification */
#define ELF_MAGIC0  0x7Fu
#define ELF_MAGIC1  'E'
#define ELF_MAGIC2  'L'
#define ELF_MAGIC3  'F'

#define ELF_CLASS32  1u
#define ELF_DATA2LSB 1u   /* little-endian */
#define ELF_ET_EXEC  2u   /* executable file */
#define ELF_EM_386   3u   /* Intel 80386 */
#define ELF_PT_LOAD  1u   /* loadable segment */

/* ELF32 header (52 bytes) */
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;      /* entry-point virtual address */
    uint32_t e_phoff;      /* program-header table offset */
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;      /* number of program-header entries */
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf32_ehdr_t;

/* ELF32 program header (32 bytes) */
typedef struct {
    uint32_t p_type;
    uint32_t p_offset;   /* offset of segment data in file */
    uint32_t p_vaddr;    /* virtual address to load into   */
    uint32_t p_paddr;
    uint32_t p_filesz;   /* bytes in file                  */
    uint32_t p_memsz;    /* bytes in memory (>= p_filesz)  */
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) elf32_phdr_t;

/*
 * elf_load – parse @data (length @len bytes) as an ELF32 binary and copy
 *            all PT_LOAD segments to their virtual addresses.
 *
 * Returns the entry-point address on success, or 0 on error.
 */
uint32_t elf_load(const void *data, uint32_t len);

/*
 * elf_load_user – load an ELF32 binary into a user-mode page directory.
 *
 * For each PT_LOAD segment, physical frames are allocated (via PMM) and
 * mapped into @pd_phys at the segment's virtual address with the
 * PAGE_USER flag set.  Segment data is written to the physical frames
 * (accessible via the kernel identity map).  BSS pages are zeroed.
 *
 * @data    : pointer to the ELF32 binary in kernel memory
 * @len     : size of the binary in bytes
 * @pd_phys : physical address of the destination page directory
 *            (must have been created with paging_create_pd())
 *
 * Returns the user-mode entry-point virtual address, or 0 on error.
 */
uint32_t elf_load_user(const void *data, uint32_t len, uint32_t pd_phys);

/*
 * User address-space layout constants.
 *
 * User-mode ELF binaries should be linked to load at USER_LOAD_BASE.
 * The user stack occupies USER_STACK_PAGES × 4 KB just above USER_LOAD_BASE,
 * growing downward from USER_STACK_TOP.
 */
#define USER_LOAD_BASE   0x01000000u   /* ELF virtual load address  */
#define USER_STACK_TOP   0x01020000u   /* user stack grows down from here */
#define USER_STACK_PAGES 4u            /* 16 KB user stack          */
#define USER_STACK_BASE  (USER_STACK_TOP - USER_STACK_PAGES * 4096u)

#endif /* ELF_H */
