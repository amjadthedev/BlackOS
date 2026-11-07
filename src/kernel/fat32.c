/* =============================================================================
 *  Black OS - Phase 10: FAT32 parser (read-only mount over a block device)
 *
 *  Flow: MBR -> partition table -> BPB -> cache FAT -> walk root cluster.
 *  Long file names (0x0F entries) are skipped; 8.3 short names are honoured.
 *  Files/dirs are grafted as disk-backed VFS nodes via vfs_link_child().
 *  Maximum supported cluster size: 4096 bytes (FAT32 with 2 KiB/4 KiB
 *  clusters covers QEMU test volumes).
 * ========================================================================== */
#include "vfs.h"
#include "fat32.h"
#include "bdev.h"
#include "pmm.h"
#include "string.h"
#include "printk.h"

#pragma pack(push, 1)
struct mbr_part {                       /* at 0x1BE + 16*i in sector 0      */
    u8  flags;
    u8  chs_first[3];
    u8  type;
    u8  chs_last[3];
    u32 lba;
    u32 sectors;
};

struct fat32_bpb {                      /* BPB, sector 0 of the volume      */
    u8  jump[3];
    char oem[8];
    u16 bytes_per_sector;
    u8  sectors_per_cluster;
    u16 reserved_sectors;
    u8  fats;
    u16 root_entries_unused;
    u16 total16_unused;
    u8  media;
    u16 fat_size_12_16;
    u16 drive_desc_unused;
    u16 reserved_unused;
    u32 hidden;
    u32 total_sectors;
    u32 fat_size_32;
    u16 ext_flags;
    u16 fs_version;
    u32 root_cluster;
    u16 fsinfo_sector;
    u16 backup_sector;
    u8  reserved12[12];
    u8  drive_no;
    u8  flags_nt;
    u8  boot_sig;
    u32 volume_id;
    char volume_label[11];
};
#pragma pack(pop)

struct fat32_vol {
    struct bdev *dev;
    u64          part_lba;
    u32          bytes_per_sector;
    u32          sectors_per_cluster;
    u32          reserved;
    u32          fats;
    u32          fat_size;             /* sectors per FAT                  */
    u32          root_cluster;
    u32          cluster_bytes;
    u32         *fat_cache;            /* cached FAT, identity RAM         */
    u32          fat_frames;           /* PMM run size held                */
};

#define FAT32_ATTR_DIR       0x10
#define FAT32_ATTR_LONGNAME  0x0F

static u32 fat32_next_cluster(struct fat32_vol *v, u32 cluster)
{
    return v->fat_cache[cluster] & 0x0FFFFFFFu;
}

static u64 fat32_cluster_lba(struct fat32_vol *v, u32 cluster)
{
    return v->part_lba + v->reserved + (u64)v->fats * v->fat_size
         + (u64)(cluster - 2) * v->sectors_per_cluster;
}

static void fat32_free_vol(struct fat32_vol *v)
{
    if (v->fat_cache != NULL)
        pmm_free_run((u64)(usize)v->fat_cache, v->fat_frames);
}

static void fat32_name_of(const u8 *dirent, char out[13])
{
    char base[9];
    char ext[4];
    memcpy(base, dirent, 8);
    memcpy(ext, dirent + 8, 3);
    base[8] = ext[3] = '\0';
    for (int i = 7; i >= 0 && base[i] == ' '; i--)
        base[i] = '\0';
    for (int i = 2; i >= 0 && ext[i] == ' '; i--)
        ext[i] = '\0';

    int o = 0;
    for (int i = 0; base[i] && o < 9; i++)
        out[o++] = (char)((base[i] >= 'A' && base[i] <= 'Z')
                          ? base[i] + ('a' - 'A') : base[i]);
    if (ext[0]) {
        out[o++] = '.';
        for (int i = 0; ext[i] && o < 12; i++)
            out[o++] = (char)((ext[i] >= 'A' && ext[i] <= 'Z')
                              ? ext[i] + ('a' - 'A') : ext[i]);
    }
    out[o] = '\0';
}

static void fat32_walk(struct fat32_vol *v, vfs_handle dir,
                       u32 cluster, int depth)
{
    if (depth > 6)
        return;

    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        u32 frames = (v->cluster_bytes + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
        u8 *area = (u8 *)(usize)pmm_alloc_run(frames);
        if (area == NULL)
            return;
        if (v->dev->read(v->dev->ctx, fat32_cluster_lba(v, cluster),
                         v->sectors_per_cluster, area) != 0) {
            pmm_free_run((u64)(usize)area, frames);
            return;
        }

        for (u32 off = 0; off + 32 <= v->cluster_bytes; off += 32) {
            const u8 *e = area + off;
            u8 first_byte = e[0];
            u8 attr = e[11];

            if (first_byte == 0x00)      /* sentinel: no more entries      */
                goto cluster_done;
            if (first_byte == 0xE5)      /* deleted                        */
                continue;
            if (attr & FAT32_ATTR_LONGNAME)
                continue;

            u32 clus_hi = (u32)e[20] | ((u32)e[21] << 8);
            u32 clus_lo = (u32)e[26] | ((u32)e[27] << 8);
            u32 first = (clus_hi << 16) | clus_lo;
            if (first == 0)
                continue;                /* volume label / empty           */

            u32 fsize = (u32)e[28] | ((u32)e[29] << 8)
                      | ((u32)e[30] << 16) | ((u32)e[31] << 24);

            char nm[13];
            fat32_name_of(e, nm);

            if (attr & FAT32_ATTR_DIR) {
                vfs_handle child = vfs_link_child(dir, nm, NULL, 0, 0, 1);
                if (child >= 0)
                    fat32_walk(v, child, first, depth + 1);
            } else {
                vfs_link_child(dir, nm, v->dev,
                               fat32_cluster_lba(v, first), fsize, 0);
            }
        }
cluster_done:
        pmm_free_run((u64)(usize)area, frames);
        cluster = fat32_next_cluster(v, cluster);
    }
}

int fat32_mount(struct bdev *dev, vfs_handle vfs_dir)
{
    static u8 sector[512];
    struct fat32_vol vol;

    memset(&vol, 0, sizeof(vol));
    vol.dev = dev;

    /* 1) locate a FAT32 partition in the MBR                                */
    u64 part_lba = 0;
    int found = 0;
    if (dev->read(dev->ctx, 0, 1, sector) == 0) {
        const struct mbr_part *p =
            (const struct mbr_part *)(sector + 0x1BE);
        for (int i = 0; i < 4; i++) {
            if (p[i].type == 0x0B || p[i].type == 0x0C) {
                part_lba = p[i].lba;
                found = 1;
                break;
            }
        }
    }
    if (!found)
        return -1;                       /* no FAT32 structure on this disk */

    /* 2) parse the BPB                                                      */
    if (dev->read(dev->ctx, part_lba, 1, sector) != 0)
        return -1;
    const struct fat32_bpb *bpb = (const struct fat32_bpb *)sector;

    vol.part_lba          = part_lba;
    vol.bytes_per_sector  = bpb->bytes_per_sector;
    vol.sectors_per_cluster = bpb->sectors_per_cluster;
    vol.reserved          = bpb->reserved_sectors;
    vol.fats              = bpb->fats;
    vol.fat_size          = bpb->fat_size_32;
    vol.root_cluster      = bpb->root_cluster;
    vol.cluster_bytes     = (u32)vol.bytes_per_sector * vol.sectors_per_cluster;

    if (vol.bytes_per_sector != 512 ||
        vol.sectors_per_cluster == 0 ||
        vol.fats == 0 || vol.fat_size == 0)
        return -1;
    if (vol.cluster_bytes > 4096 || vol.cluster_bytes == 0)
        return -1;

    /* 3) cache the first FAT (FAT32 keeps only one live table active)       */
    u32 fat_bytes = vol.fat_size * vol.bytes_per_sector;
    vol.fat_frames = (fat_bytes + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    vol.fat_cache = (u32 *)(usize)pmm_alloc_run(vol.fat_frames);
    if (vol.fat_cache == NULL)
        return -1;
    if (dev->read(dev->ctx, part_lba + vol.reserved, vol.fat_size,
                  vol.fat_cache) != 0) {
        fat32_free_vol(&vol);
        return -1;
    }

    /* 4) walk the tree                                                      */
    fat32_walk(&vol, vfs_dir, vol.root_cluster, 0);
    fat32_free_vol(&vol);
    return 0;
}
