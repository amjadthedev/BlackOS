/* =============================================================================
 *  Black OS - Phase 10: FAT32 parser (read-only mount over a block device)
 *  Long file names are skipped; 8.3 short names are honoured.
 * ========================================================================== */
#ifndef BLACKOS_FAT32_H
#define BLACKOS_FAT32_H

#include "types.h"
#include "vfs.h"
struct bdev;                            /* defined in bdev.h */

/* Populates `mount` with the FAT32 tree (files become device-backed nodes).
 * Returns 0 on success.  mount must already exist and be a directory.      */
int fat32_mount(struct bdev *bdev, vfs_handle volume);

#endif /* BLACKOS_FAT32_H */
