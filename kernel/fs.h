#ifndef FS_H
#define FS_H

#include <stdint.h>

/*
 * VESPER OS – VesperFS  (simple flat on-disk filesystem)
 *
 * Layout on disk (all LBAs relative to FS_START_LBA):
 *
 *   LBA +0  : Superblock   (1 sector)
 *   LBA +1  : Directory    (FS_DIR_SECTORS sectors, 8 × 512 / 64 = 64 entries)
 *   LBA +9  : File data    (one contiguous region per file)
 *
 * Superblock (first 16 bytes of sector 0):
 *   magic[8]   : "VESPERFS"
 *   version[4] : 0x00000001
 *   num_files[4]: current file count
 *
 * Directory entry (64 bytes):
 *   name[32]       : file name (NUL-padded)
 *   start_lba[4]   : first data LBA (absolute)
 *   size[4]        : file size in bytes
 *   flags[4]       : 0 = file, 1 = deleted
 *   reserved[20]   : zeroed
 */

/* Absolute LBA where VesperFS begins on the disk image */
#define FS_START_LBA      129u   /* sectors 0=boot, 1-128=kernel, 129+=fs */

#define FS_DIR_SECTORS    8u
#define FS_MAX_FILES      64u    /* (FS_DIR_SECTORS * 512) / sizeof(fs_dirent_t) */
#define FS_NAME_MAX       31u    /* max name length (+ NUL) */
#define FS_MAX_LBA_EXCL   8192u  /* disk image size in sectors */

/* Directory entry as stored on disk */
typedef struct {
    char     name[32];
    uint32_t start_lba;
    uint32_t size;
    uint32_t flags;       /* 0=valid, 1=deleted */
    uint8_t  reserved[20];
} __attribute__((packed)) fs_dirent_t;

/* In-kernel file handle */
typedef struct {
    uint32_t start_lba;
    uint32_t size;
    uint32_t pos;          /* current read position in bytes */
    int      valid;
} fs_file_t;

/*
 * fs_init – verify the superblock.
 * Returns 1 if a valid VesperFS is found, 0 otherwise.
 * Initialises the filesystem on disk if the superblock is missing
 * (first-time setup via mkfs).
 */
int fs_init(void);

/*
 * fs_format – write a fresh empty superblock and blank directory to disk.
 * Destroys any existing data.
 */
int fs_format(void);

/* List all files to the VGA console */
void fs_ls(void);

/*
 * fs_open – open a file by name for reading.
 * Returns 0 on success, -1 if not found.
 */
int fs_open(fs_file_t *f, const char *name);

/* Read up to @len bytes into @buf from the current position.
 * Returns number of bytes actually read (0 = EOF). */
uint32_t fs_read(fs_file_t *f, void *buf, uint32_t len);

/* Close (reset) a file handle */
void fs_close(fs_file_t *f);

/*
 * fs_write_new – create or overwrite a file with the given content.
 * Returns 0 on success, -1 on error.
 */
int fs_write_new(const char *name, const void *data, uint32_t len);

/*
 * fs_delete – mark a file as deleted in the directory.
 * Returns 0 on success, -1 if the file was not found.
 */
int fs_delete(const char *name);

#endif /* FS_H */
