/* =============================================================================
 *  Black OS - minimal Multiboot2 information-structure definitions
 *  (spec: https://www.gnu.org/software/grub/manual/multiboot2/)
 * ========================================================================== */
#ifndef BLACKOS_MULTIBOOT2_H
#define BLACKOS_MULTIBOOT2_H

#include "types.h"

#define MB2_BOOT_MAGIC      0x36D76289ULL   /* value in EAX at entry */

struct mb2_info {
    u32 total_size;
    u32 reserved;
    /* u8  tags[][] follows */
};

struct mb2_tag {
    u32 type;
    u32 size;
};

#define MB2_TAG_END            0
#define MB2_TAG_CMDLINE        1
#define MB2_TAG_BOOTNAME       2
#define MB2_TAG_BASIC_MEMINFO  4
#define MB2_TAG_MMAP           6

#pragma pack(push, 1)
struct mb2_basemem {                       /* MB2_TAG_BASIC_MEMINFO */
    u32 type;
    u32 size;
    u32 mem_lower;      /* KiB below 1 MiB  */
    u32 mem_upper;      /* KiB above 1 MiB  */
};

struct mb2_mmap {                          /* MB2_TAG_MMAP */
    u32 type;
    u32 size;
    u32 entry_size;
    u32 entry_version;
    /* struct mb2_mmap_entry entries[] follows */
};

struct mb2_mmap_entry {
    u64 addr;
    u64 len;
    u32 type;           /* 1 == available RAM */
    u32 reserved;
};
#pragma pack(pop)

#define MB2_MMAP_AVAILABLE     1
#define MB2_MMAP_RESERVED      2
#define MB2_MMAP_ACPI          3
#define MB2_MMAP_NVS           4
#define MB2_MMAP_BAD           5

/* Tags are 8-byte aligned; advance to the next tag with this helper. */
static inline u32 mb2_tag_step(u32 size)
{
    return (size + 7u) & ~7u;
}

/* ---------------------------------------------------------------------------
 * Multiboot v1 — the protocol actually used at boot.
 *
 * GRUB 2.06's i386-pc `multiboot` command speaks Multiboot v1 (it refuses to
 * ELF-load our x86-64 image for multiboot2), so boot.s boots the raw binary
 * through the MB1 a.out-kludge header and hands kmain whatever EAX the
 * loader left behind.  The MB2 tag definitions above stay valid for the
 * MB2 header that is still embedded in the image.
 * ------------------------------------------------------------------------- */
#define MB1_BOOT_MAGIC      0x2BADB002ULL   /* value in EAX at MB1 entry     */

struct mb1_info {                           /* flat structure at EBX         */
    u32 flags;
    u32 mem_lower;      /* [bit 0] KiB of conventional memory below 1 MiB   */
    u32 mem_upper;      /* [bit 0] KiB of memory starting at 1 MiB          */
    u32 boot_device;
    u32 cmdline;
    u32 mods_count;
    u32 mods_addr;
    u32 syms[4];
    u32 mmap_length;    /* [bit 6] size of the mmap buffer                   */
    u32 mmap_addr;      /* [bit 6] physical address of the mmap buffer       */
    u32 drives_length;
    u32 drives_addr;
    u32 config_table;
    u32 boot_loader_name;
    u32 apm_table;
    u32 vbe_control_info;
    u32 vbe_mode_info;
    u16 vbe_mode;
    u16 vbe_interface_seg;
    u16 vbe_interface_off;
    u16 vbe_interface_len;
};

#define MB1_FLAG_MEM        (1u << 0)
#define MB1_FLAG_MMAP       (1u << 6)
#define MB1_MMAP_AVAILABLE  1

#pragma pack(push, 1)
struct mb1_mmap_entry {                     /* entries at mb1_info.mmap_addr */
    u32 size;           /* bytes AFTER this field (so step = size + 4)       */
    u64 addr;
    u64 len;
    u32 type;           /* 1 == available RAM                               */
};
#pragma pack(pop)

#endif /* BLACKOS_MULTIBOOT2_H */
