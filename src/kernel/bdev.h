/* =============================================================================
 *  Black OS - Phase 10: minimal block device interface
 * ========================================================================== */
#ifndef BLACKOS_BDEV_H
#define BLACKOS_BDEV_H

#include "types.h"

struct bdev {
    /* 0 on success, negative on error; `buf` receives count * sector_size */
    int  (*read)(void *ctx, u64 lba, u32 count, void *buf);
    u32  sector_size;
    void *ctx;
};

#endif /* BLACKOS_BDEV_H */
