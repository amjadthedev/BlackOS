/* =============================================================================
 *  Black OS - Phase 10: Virtual File System
 *
 *  Layering: ramfs (mutable in-memory files) + optional FAT32 volume mounted
 *  (MOUNTED at /hd by the mountfat command, which populates not-really data;
 *   the FAT32 driver reads raw sectors via a bound block device).
 * ========================================================================== */
#ifndef BLACKOS_VFS_H
#define BLACKOS_VFS_H

#include "types.h"

struct bdev;                            /* defined in bdev.h */

#define VFS_MAX_NODES   96

typedef int vfs_handle;                 /* -1 on failure             */

int       vfs_init(void);
vfs_handle vfs_mkdir(const char *path);
vfs_handle vfs_open(const char *path);    /* create-or-find a file node     */
vfs_handle vfs_lookup(const char *path);
const char *vfs_name(vfs_handle node);
int       vfs_is_dir(vfs_handle node);
u32       vfs_dir_count(vfs_handle dir);
vfs_handle vfs_dir_at(vfs_handle dir, u32 idx);
u64       vfs_size(vfs_handle file);
u64       vfs_read(vfs_handle file, void *buf, u64 len, u64 off);
u64       vfs_write(vfs_handle file, const void *buf, u64 len);
/* used by the FAT32 mounter to graft disk-backed nodes into the tree     */
vfs_handle vfs_link_child(vfs_handle parent, const char *name,
                          struct bdev *dev, u64 lba, u64 size, int is_dir);
void      vfs_debug_list(void);
int       vfs_mount(vfs_handle mount_point, struct bdev *dev);

#endif /* BLACKOS_VFS_H */
