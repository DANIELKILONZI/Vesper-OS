#include "fs.h"
#include "ata.h"
#include "string.h"
#include "vga.h"

/* -------------------------------------------------------------------------
 * On-disk layout constants
 * ---------------------------------------------------------------------- */
#define FS_SUPER_LBA    (FS_START_LBA + 0u)   /* superblock sector     */
#define FS_DIR_LBA      (FS_START_LBA + 1u)   /* first directory sector */
#define FS_DATA_LBA     (FS_START_LBA + 1u + FS_DIR_SECTORS)  /* data area */

/* Bytes in the directory region */
#define FS_DIR_BYTES    (FS_DIR_SECTORS * ATA_SECTOR_SIZE)

/* Superblock magic and version */
static const char FS_MAGIC[8] = { 'V','E','S','P','E','R','F','S' };
#define FS_VERSION  1u

/* Superblock (fits in the first 16 bytes of a 512-byte sector) */
typedef struct {
    char     magic[8];
    uint32_t version;
    uint32_t num_files;
    uint8_t  pad[512 - 16];
} __attribute__((packed)) fs_super_t;

/* -------------------------------------------------------------------------
 * Sector-sized scratch buffers (static to avoid large stack frames)
 * ---------------------------------------------------------------------- */
static uint8_t  sector_buf[ATA_SECTOR_SIZE];

/* -------------------------------------------------------------------------
 * Helper: read the superblock
 * ---------------------------------------------------------------------- */
static int read_super(fs_super_t *sb)
{
    if (ata_read_sectors(FS_SUPER_LBA, 1, sb) != 0) {
        return -1;
    }
    return 0;
}

/* Helper: write the superblock */
static int write_super(const fs_super_t *sb)
{
    return ata_write_sectors(FS_SUPER_LBA, 1, sb);
}

/* -------------------------------------------------------------------------
 * Helper: read/write the full directory into a caller-supplied buffer.
 * The buffer must be FS_DIR_BYTES (FS_DIR_SECTORS × 512) bytes.
 * ---------------------------------------------------------------------- */
static uint8_t dir_buf[FS_DIR_BYTES];

static int read_dir(void)
{
    return ata_read_sectors(FS_DIR_LBA, (uint8_t)FS_DIR_SECTORS, dir_buf);
}

static int write_dir(void)
{
    return ata_write_sectors(FS_DIR_LBA, (uint8_t)FS_DIR_SECTORS, dir_buf);
}

static inline fs_dirent_t *dir_entry(uint32_t i)
{
    return (fs_dirent_t *)(dir_buf + i * sizeof(fs_dirent_t));
}

/* -------------------------------------------------------------------------
 * fs_format – write blank superblock + empty directory
 * ---------------------------------------------------------------------- */
int fs_format(void)
{
    fs_super_t sb;
    memset(&sb, 0, sizeof(sb));
    memcpy(sb.magic, FS_MAGIC, 8);
    sb.version   = FS_VERSION;
    sb.num_files = 0;

    if (write_super(&sb) != 0) {
        return -1;
    }

    /* Blank directory */
    memset(dir_buf, 0, sizeof(dir_buf));
    return write_dir();
}

/* -------------------------------------------------------------------------
 * fs_init – check for a valid superblock
 * ---------------------------------------------------------------------- */
int fs_init(void)
{
    fs_super_t sb;
    if (read_super(&sb) != 0) {
        return 0;
    }
    return (memcmp(sb.magic, FS_MAGIC, 8) == 0 && sb.version == FS_VERSION);
}

/* -------------------------------------------------------------------------
 * fs_ls – list files to VGA console
 * ---------------------------------------------------------------------- */
void fs_ls(void)
{
    if (read_dir() != 0) {
        vga_puts("  [FS] disk read error\n");
        return;
    }

    int found = 0;
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_puts("NAME                             SIZE\n");
    vga_puts("-------------------------------- ------\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    for (uint32_t i = 0; i < FS_MAX_FILES; i++) {
        const fs_dirent_t *e = dir_entry(i);
        if (e->name[0] == '\0' || e->flags == 1u) {
            continue;
        }
        found = 1;
        /* Pad name to 32 chars */
        uint32_t nlen = strlen(e->name);
        vga_puts(e->name);
        for (uint32_t j = nlen; j < 32u; j++) {
            vga_putchar(' ');
        }
        vga_print_uint(e->size);
        vga_puts(" B\n");
    }

    if (!found) {
        vga_puts("  (empty)\n");
    }
}

/* -------------------------------------------------------------------------
 * fs_open – find a file by name and fill in the file handle
 * ---------------------------------------------------------------------- */
int fs_open(fs_file_t *f, const char *name)
{
    if (read_dir() != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < FS_MAX_FILES; i++) {
        const fs_dirent_t *e = dir_entry(i);
        if (e->name[0] == '\0' || e->flags == 1u) {
            continue;
        }
        if (strcmp(e->name, name) == 0) {
            f->start_lba = e->start_lba;
            f->size      = e->size;
            f->pos       = 0;
            f->valid     = 1;
            return 0;
        }
    }
    return -1;
}

/* -------------------------------------------------------------------------
 * fs_read – read bytes sequentially from an open file
 * ---------------------------------------------------------------------- */
uint32_t fs_read(fs_file_t *f, void *buf, uint32_t len)
{
    if (!f || !f->valid || f->pos >= f->size) {
        return 0;
    }

    /* Clamp to remaining bytes */
    if (len > f->size - f->pos) {
        len = f->size - f->pos;
    }

    uint32_t  remaining  = len;
    uint8_t  *out        = (uint8_t *)buf;

    while (remaining > 0) {
        /* Which sector within the file? */
        uint32_t file_sector   = f->pos / ATA_SECTOR_SIZE;
        uint32_t sector_offset = f->pos % ATA_SECTOR_SIZE;
        uint32_t disk_lba      = f->start_lba + file_sector;

        if (ata_read_sectors(disk_lba, 1, sector_buf) != 0) {
            break;
        }

        uint32_t chunk = ATA_SECTOR_SIZE - sector_offset;
        if (chunk > remaining) {
            chunk = remaining;
        }

        memcpy(out, sector_buf + sector_offset, chunk);
        out       += chunk;
        f->pos    += chunk;
        remaining -= chunk;
    }

    return len - remaining;
}

/* -------------------------------------------------------------------------
 * fs_close – invalidate the file handle
 * ---------------------------------------------------------------------- */
void fs_close(fs_file_t *f)
{
    if (f) {
        f->valid = 0;
    }
}

/* -------------------------------------------------------------------------
 * fs_write_new – create or overwrite a file
 * ---------------------------------------------------------------------- */
int fs_write_new(const char *name, const void *data, uint32_t len)
{
    if (!name || name[0] == '\0') {
        return -1;
    }

    /* Read superblock and directory */
    fs_super_t sb;
    if (read_super(&sb) != 0 || read_dir() != 0) {
        return -1;
    }
    if (memcmp(sb.magic, FS_MAGIC, 8) != 0) {
        return -1;   /* filesystem not formatted */
    }

    /*
     * Find a free or matching (overwrite) directory slot.
     * Also track the highest used data LBA to place new data after it.
     * Additionally, try to reuse space from a deleted file that has enough
     * sectors to hold the new file (simple space reclamation).
     */
    int      target_slot    = -1;
    int      is_overwrite   = 0;       /* 1 when replacing an existing file */
    uint32_t next_data_lba  = FS_DATA_LBA;
    uint32_t sectors_needed = (len + ATA_SECTOR_SIZE - 1u) / ATA_SECTOR_SIZE;
    if (sectors_needed == 0u) {
        sectors_needed = 1u;   /* always consume at least one sector */
    }

    /* First pass: find best-fit among deleted entries for space reuse */
    int      reuse_slot     = -1;
    uint32_t reuse_lba      = 0u;
    uint32_t reuse_sectors  = 0xFFFFFFFFu;   /* track best (smallest fit) */

    for (uint32_t i = 0; i < FS_MAX_FILES; i++) {
        const fs_dirent_t *e = dir_entry(i);

        if (e->name[0] != '\0' && e->flags != 1u) {
            /* Active entry – check if we're overwriting it */
            if (strcmp(e->name, name) == 0) {
                target_slot  = (int)i;
                is_overwrite = 1;
            }
            /* Track end of this file's data to avoid overlap */
            uint32_t end = e->start_lba +
                           (e->size + ATA_SECTOR_SIZE - 1u) / ATA_SECTOR_SIZE;
            if (end > next_data_lba) {
                next_data_lba = end;
            }
        } else {
            /* Free or deleted slot */
            if (target_slot < 0 && !is_overwrite) {
                target_slot = (int)i;   /* first free/deleted slot */
            }
            /* Check if this deleted entry has reusable data space */
            if (e->flags == 1u && e->start_lba >= FS_DATA_LBA && e->size > 0u) {
                uint32_t avail = (e->size + ATA_SECTOR_SIZE - 1u) / ATA_SECTOR_SIZE;
                if (avail >= sectors_needed && avail < reuse_sectors) {
                    reuse_slot    = (int)i;
                    reuse_lba     = e->start_lba;
                    reuse_sectors = avail;
                }
            }
        }
    }

    if (target_slot < 0) {
        return -1;   /* directory full */
    }

    /*
     * If we found a deleted entry with enough space, reuse its LBA range
     * instead of always appending.  Use the reuse_slot as the directory entry
     * unless we already have an overwrite target.
     */
    uint32_t write_lba = next_data_lba;   /* default: append at end */
    if (!is_overwrite && reuse_slot >= 0) {
        write_lba   = reuse_lba;
        target_slot = reuse_slot;   /* reuse the directory entry too */
    }

    /* Write the file data sector by sector */
    const uint8_t *src = (const uint8_t *)data;
    uint32_t written = 0;

    for (uint32_t s = 0; s < sectors_needed; s++) {
        memset(sector_buf, 0, ATA_SECTOR_SIZE);
        uint32_t chunk = len - written;
        if (chunk > ATA_SECTOR_SIZE) {
            chunk = ATA_SECTOR_SIZE;
        }
        memcpy(sector_buf, src + written, chunk);
        if (ata_write_sectors(write_lba + s, 1, sector_buf) != 0) {
            return -1;
        }
        written += chunk;
    }

    /* Update directory entry */
    fs_dirent_t *e = dir_entry((uint32_t)target_slot);
    memset(e, 0, sizeof(*e));
    strncpy(e->name, name, FS_NAME_MAX);
    e->name[FS_NAME_MAX] = '\0';
    e->start_lba = write_lba;
    e->size      = len;
    e->flags     = 0;

    if (write_dir() != 0) {
        return -1;
    }

    /* Update superblock num_files only when a new file is created */
    if (!is_overwrite) {
        sb.num_files++;
    }
    return write_super(&sb);
}

/* -------------------------------------------------------------------------
 * fs_delete – mark a file as deleted in the directory
 * ---------------------------------------------------------------------- */
int fs_delete(const char *name)
{
    if (!name || name[0] == '\0') {
        return -1;
    }

    if (read_dir() != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < FS_MAX_FILES; i++) {
        fs_dirent_t *e = dir_entry(i);
        if (e->name[0] == '\0' || e->flags == 1u) {
            continue;
        }
        if (strcmp(e->name, name) == 0) {
            e->flags = 1u;   /* mark as deleted */
            if (write_dir() != 0) {
                return -1;
            }

            /* Decrement num_files in the superblock */
            fs_super_t sb;
            if (read_super(&sb) == 0 && sb.num_files > 0) {
                sb.num_files--;
                write_super(&sb);
            }
            return 0;
        }
    }

    return -1;   /* file not found */
}
