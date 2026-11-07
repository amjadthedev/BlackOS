/* =============================================================================
 *  Black OS - package.c : .pkg parsing + install + launch
 * ========================================================================== */
#include "package.h"
#include "vfs.h"
#include "pmm.h"
#include "string.h"
#include "syscall.h"
#include "printk.h"

int pkg_parse(const void *data, u64 len, struct pkg_package *out)
{
    if (data == NULL || out == NULL || len < sizeof(struct pkg_header))
        return -1;
    const struct pkg_header *h = (const struct pkg_header *)data;
    if (memcmp(h->magic, PKG_MAGIC, 8) != 0)
        return -1;

    memcpy(&out->hdr, h, sizeof(struct pkg_header));

    u64 entry_off = h->entry_off;
    u64 entry_len = h->entry_len;
    if (entry_off + entry_len > len)
        return -1;

    out->entry = (void *)((const u8 *)data + entry_off);
    if (h->icon_off && h->icon_off + h->icon_len <= len)
        out->icon = (void *)((const u8 *)data + h->icon_off);
    else
        out->icon = NULL;
    return 0;
}

void pkg_free(struct pkg_package *pkg)
{
    (void)pkg;
    /* blobs point into the caller's buffer; nothing owned here              */
}

int pkg_install(const char *vfs_dest, const void *data, u64 len)
{
    if (vfs_dest == NULL || data == NULL || len == 0)
        return -1;
    vfs_handle h = vfs_open(vfs_dest);
    if (h < 0)
        return -1;
    u64 wrote = vfs_write(h, data, len);
    return (wrote == len) ? 0 : -1;
}

int pkg_launch(struct pkg_package *pkg)
{
    if (pkg == NULL || pkg->entry == NULL || pkg->hdr.entry_len == 0)
        return -1;
    /* run the code blob in ring 3 via the syscall ABI                        */
    u64 rc = syscall_run_task(pkg->entry, pkg->hdr.entry_len, 0, 0);
    printk("[pkg ] task exited with %llu\n", rc);
    return 0;
}
