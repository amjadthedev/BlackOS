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

#endif /* BLACKOS_MULTIBOOT2_H */
