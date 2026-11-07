/* =============================================================================
 *  Black OS - package.h / package.c : .pkg application packages
 *
 *  Format (little-endian, all fields packed):
 *      offset 0  : magic[8]   = "BLKPKG\0"
 *      offset 8  : u32 version
 *      offset 12 : u32 entry_off  (file offset of code blob)
 *      offset 16 : u32 entry_len  (bytes of code blob)
 *      offset 20 : u32 icon_off
 *      offset 24 : u32 icon_len
 *      offset 28 : char name[64]
 *      offset 92 : char desc[128]
 *      offset 220: code blob + icon blob
 * ========================================================================== */
#ifndef BLACKOS_PACKAGE_H
#define BLACKOS_PACKAGE_H

#include "types.h"

#define PKG_MAGIC "BLKPKG\0"
#define PKG_NAME_MAX 64
#define PKG_DESC_MAX 128

#pragma pack(push, 1)
struct pkg_header {
    char  magic[8];
    u32   version;
    u32   entry_off;
    u32   entry_len;
    u32   icon_off;
    u32   icon_len;
    char  name[PKG_NAME_MAX];
    char  desc[PKG_DESC_MAX];
};
#pragma pack(pop)

struct pkg_package {
    struct pkg_header hdr;
    void   *entry;      /* code blob (PMM frame)            */
    void   *icon;       /* icon blob (optional)             */
};

int  pkg_parse(const void *data, u64 len, struct pkg_package *out);
void pkg_free(struct pkg_package *pkg);
int  pkg_install(const char *vfs_dest, const void *data, u64 len);
int  pkg_launch(struct pkg_package *pkg);   /* ring-3 via syscall_run_task      */

#endif /* BLACKOS_PACKAGE_H */