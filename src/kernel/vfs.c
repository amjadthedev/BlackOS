/* =============================================================================
 *  Black OS - Phase 10: Virtual File System (ramfs + read-only FAT32 mount)
 * ========================================================================== */
#include "vfs.h"
#include "bdev.h"
#include "fat32.h"
#include "pmm.h"
#include "string.h"
#include "printk.h"

#define VFS_FLAG_DIR   0x1

struct vnode {
    char             name[32];
    u32              flags;
    struct vnode    *first_child;
    struct vnode    *next_sibling;
    u64              size;

    /* file payload: either in-RAM pages or a FAT32-backed read window     */
    void            *data;                  /* zero when disk-backed      */
    struct bdev     *dev;                   /* when disk-backed           */
    u64              dev_lba;               /* first LBA of the file      */
};

static struct vnode nodes[VFS_MAX_NODES];
static struct vnode *root;
static u32 nodes_used;

/* ---------------------------------------------------------------- helpers --- */
static struct vnode *node_alloc(void)
{
    if (root == 0 && nodes_used < VFS_MAX_NODES) {
        memset(&nodes[nodes_used], 0, sizeof(struct vnode));
        return &nodes[nodes_used++];
    }
    for (u32 i = 0; i < VFS_MAX_NODES; i++) {
        struct vnode *n = &nodes[i];
        if (n->name[0] == 0 && n->first_child == NULL &&
            n->next_sibling == NULL && n->data == NULL && n->dev == NULL) {
            memset(n, 0, sizeof(struct vnode));
            return n;
        }
    }
    return NULL;
}

static struct vnode *child_of(struct vnode *parent, const char *name)
{
    for (struct vnode *c = parent->first_child; c; c = c->next_sibling)
        if (strcmp(c->name, name) == 0)
            return c;
    return NULL;
}

static struct vnode *mkdir_node(struct vnode *parent, const char *name)
{
    struct vnode *n = node_alloc();
    if (n == NULL)
        return NULL;
    strncpy(n->name, name, sizeof(n->name) - 1);
    n->flags = VFS_FLAG_DIR;

    if (parent == NULL) {
        root = n;
        return n;
    }
    n->next_sibling = parent->first_child;
    parent->first_child = n;
    return n;
}

int vfs_init(void)
{
    memset(nodes, 0, sizeof(nodes));
    nodes_used = 0;
    root = NULL;

    struct vnode *r = mkdir_node(NULL, "/");
    if (r == NULL)
        return -1;

    /* scaffold: a tiny HOME tree lived in 4KB page bursts                  */
    vfs_mkdir("/bin");
    vfs_mkdir("/etc");
    vfs_mkdir("/home");
    vfs_mkdir("/home/black");

    const char welcome[] =
        "Black OS VFS up.  Files live in RAM pages; FAT32 volumes mount\n"
        "under /hd when a boot image is attached at -drive if=ide.\n";
    vfs_handle f = vfs_open("/home/black/README.TXT");
    if (f >= 0)
        vfs_write(f, welcome, (u64)sizeof(welcome) - 1);

    return 0;
}

/* walk slash-separated path components from the root                       */
static struct vnode *path_node(const char *path_c, int create_dirs)
{
    if (root == NULL)
        return NULL;
    if (path_c[0] != '/')
        return NULL;
    if (path_c[1] == '\0')
        return root;

    struct vnode *cur = root;
    char comp[32];
    u32 ci = 0;
    const char *p = path_c + 1;

    while (*p != 0) {
        if (*p == '/') {
            comp[ci] = '\0';
            if (ci == 0) {              /* "//" collapse   */
                p++;
                continue;
            }
            struct vnode *next = child_of(cur, comp);
            if (next == NULL) {
                if (!create_dirs || !(cur->flags & VFS_FLAG_DIR))
                    return NULL;
                next = mkdir_node(cur, comp);
                if (next == NULL)
                    return NULL;
            }
            if (!(next->flags & VFS_FLAG_DIR))
                return NULL;
            cur = next;
            ci = 0;
            p++;
            continue;
        }
        if (ci < sizeof(comp) - 1)
            comp[ci++] = *p;
        p++;
    }
    comp[ci] = '\0';
    if (ci == 0)
        return cur;

    struct vnode *hit = child_of(cur, comp);
    if (hit != NULL)
        return hit;
    if (!create_dirs)
        return NULL;
    return mkdir_node(cur, comp);
}

vfs_handle vfs_lookup(const char *path_c)
{
    struct vnode *n = path_node(path_c, 0);
    return (n == NULL) ? (vfs_handle)-1 : (vfs_handle)(n - nodes);
}

vfs_handle vfs_mkdir(const char *path_c)
{
    struct vnode *n = path_node(path_c, 1);
    return (n == NULL) ? (vfs_handle)-1 : (vfs_handle)(n - nodes);
}

vfs_handle vfs_open(const char *path_c)
{
    /* Files come into being on mkdir-ish node creation when vfs_open is
     * handed a non-dir path. Existence is entailed by a lookup.            */
    struct vnode *n = path_node(path_c, 0);
    if (n != NULL)
        return (vfs_handle)(n - nodes);

    /* create file node                                                    */
    char dir_part[64];
    const char *last = NULL;
    for (const char *pp = path_c; *pp; pp++)
        if (*pp == '/')
            last = pp;
    if (last == NULL)
        return (vfs_handle)-1;
    unsigned int dl = (unsigned int)(last - path_c) + 1;
    if (dl >= sizeof(dir_part))
        return (vfs_handle)-1;
    memcpy(dir_part, path_c, dl);
    dir_part[dl] = '\0';

    struct vnode *dir = path_node(dir_part, 0);
    if (dir == NULL || !(dir->flags & VFS_FLAG_DIR))
        return (vfs_handle)-1;

    struct vnode *file = node_alloc();
    if (file == NULL)
        return (vfs_handle)-1;
    file->flags = 0;                    /* ordinary file                    */
    strncpy(file->name, last + 1, sizeof(file->name) - 1);
    file->next_sibling = dir->first_child;
    dir->first_child = file;
    return (vfs_handle)(file - nodes);
}

const char *vfs_name(vfs_handle h)
{
    if (h < 0 || h >= (vfs_handle)VFS_MAX_NODES)
        return "";
    return nodes[h].name;
}

int vfs_is_dir(vfs_handle h)
{
    if (h < 0 || h >= (vfs_handle)VFS_MAX_NODES)
        return 0;
    return (nodes[h].flags & VFS_FLAG_DIR) ? 1 : 0;
}

u32 vfs_dir_count(vfs_handle h)
{
    if (!vfs_is_dir(h))
        return (u32)-1;
    u32 n = 0;
    for (struct vnode *c = nodes[h].first_child; c; c = c->next_sibling)
        n++;
    return n;
}

vfs_handle vfs_dir_at(vfs_handle h, u32 idx)
{
    struct vnode *c = nodes[h].first_child;
    while (idx > 0 && c != NULL) {
        c = c->next_sibling;
        idx--;
    }
    return (c == NULL) ? -1 : (vfs_handle)(c - nodes);
}

/* used by the FAT32 mounter: graft a disk-backed node under `parent`      */
vfs_handle vfs_link_child(vfs_handle parent, const char *name,
                          struct bdev *dev, u64 lba, u64 size, int is_dir)
{
    if (parent < 0 || parent >= (vfs_handle)VFS_MAX_NODES)
        return (vfs_handle)-1;
    if (!vfs_is_dir(parent))
        return (vfs_handle)-1;

    struct vnode *file = node_alloc();
    if (file == NULL)
        return (vfs_handle)-1;
    strncpy(file->name, name, sizeof(file->name) - 1);
    file->flags = is_dir ? VFS_FLAG_DIR : 0;
    file->dev = dev;
    file->dev_lba = lba;
    file->size = size;
    file->next_sibling = nodes[parent].first_child;
    nodes[parent].first_child = file;
    return (vfs_handle)(file - nodes);
}

u64 vfs_size(vfs_handle file_h)
{
    if (file_h < 0 || file_h >= (vfs_handle)VFS_MAX_NODES)
        return 0;
    struct vnode *n = &nodes[file_h];
    if (n->dev != NULL)
        return n->size;
    return n->size;
}

u64 vfs_read(vfs_handle h, void *buf, u64 len, u64 off)
{
    if (h < 0 || h >= (vfs_handle)VFS_MAX_NODES || buf == NULL)
        return 0;
    struct vnode *n = &nodes[h];
    if (n->dev != NULL) {
        u32 sec = 512;
        u64 first_lba = n->dev_lba + off / sec;
        u64 last_lba  = n->dev_lba + (off + len - 1) / sec;
        u32 sects = (u32)(last_lba - first_lba + 1);
        /* bounce buffer: uncompressed bed reading into caller pages? none */
        static u8 scratch[512 * 8];     /* 4 KiB enough for this demo     */
        if (len > sizeof(scratch))
            len = sizeof(scratch);
        if (n->dev->read(n->dev->ctx, first_lba, sects, scratch) != 0)
            return 0;
        u64 copy_len = len;
        if (off + len > n->size) {
            u64 file_end = (n->size > off) ? (u64)n->size - off : 0;
            if (copy_len > file_end)
                copy_len = file_end;
        }
        memcpy(buf, scratch + (off & (sec - 1)), copy_len);
        return copy_len;
    }

    if (off >= n->size)
        return 0;
    u64 avail = n->size - off;
    u64 take = (len < avail) ? len : avail;
    memcpy(buf, (u8 *)n->data + off, take);
    return take;
}

u64 vfs_write(vfs_handle file_h, const void *buf, u64 len)
{
    if (file_h < 0 || file_h >= (vfs_handle)VFS_MAX_NODES || buf == NULL)
        return 0;
    struct vnode *n = &nodes[file_h];
    if (n->dev != NULL)
        return 0;                       /* FAT32 mounts are read-only      */

    u64 need_frames = (len + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    u64 new_frames = 0;

    if (n->data == NULL) {
        u64 have = 0;
        n->data = (void *)(usize)pmm_alloc_run(need_frames);
        if (n->data == NULL)
            return 0;
        new_frames = need_frames;
    } else if (len > n->size) {
        u64 cur_frames = (n->size + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
        if (need_frames > cur_frames) {
            void *bigger = (void *)(usize)pmm_alloc_run(need_frames);
            if (bigger == NULL)
                return 0;
            memcpy(bigger, n->data, n->size);
            pmm_free_run((u64)(usize)n->data, cur_frames);
            n->data = bigger;
        }
    }
    if (new_frames == 0)
        new_frames = need_frames;

    if (len > n->size) {
        memcpy((u8 *)n->data + n->size, buf, len - n->size);
        n->size = len;
    } else {
        memcpy((u8 *)n->data, buf, len);
        n->size = len;
    }
    (void)new_frames;
    return len;
}

void vfs_debug_list(void)
{
    extern void vfs_debug_dump_node(struct vnode *n, int depth);
    vfs_debug_dump_node(root, 0);
}

/* defined here to keep the recursion scoped out of the public VFS API */
void vfs_debug_dump_node(struct vnode *n, int depth)
{
    if (n == NULL || depth > 4)
        return;
    for (int i = 0; i < depth; i++)
        printk("  ");
    if (n->flags & VFS_FLAG_DIR) {
        printk("%s/\n", n->name);
        for (struct vnode *c = n->first_child; c != NULL; c = c->next_sibling)
            vfs_debug_dump_node(c, depth + 1);
    } else {
        printk("%s  (%llu bytes)\n", n->name, n->size);
    }
}

int vfs_mount(vfs_handle mount_point, struct bdev *dev)
{
    if (mount_point < 0 || mount_point >= (vfs_handle)VFS_MAX_NODES)
        return -1;
    if (!vfs_is_dir(mount_point))
        return -1;
    if (dev == NULL || dev->read == NULL)
        return -1;

    int rc = fat32_mount(dev, mount_point);
    if (rc == 0) {
        nodes[mount_point].flags |= VFS_FLAG_DIR;
        printk("[vfs ] mounted FAT32 volume under /%s\n", nodes[mount_point].name);
    } else {
        printk("[vfs ] FAT32 mount failed r=%d\n", rc);
    }
    return rc;
}
