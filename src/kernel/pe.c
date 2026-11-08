/* =============================================================================
*  Black OS - pe.c : Windows PE/COFF parser (Strategy 2)
* ========================================================================== */
#include "pe.h"
#include "printk.h"
#include "string.h"

int pe_parse(const void *image, u64 len, struct pe_image *info)
{
    if (info == NULL || image == NULL || len < 64)
        return -1;
    memset(info, 0, sizeof(*info));
    info->base = image;
    const dos_header_t *dh = (const dos_header_t *)image;
    if (dh->e_magic[0] != 'M' || dh->e_magic[1] != 'Z')
        return -1;
    if (dh->e_lfanew + 4 + sizeof(coff_header_t) + sizeof(optional_header_t) > (u32)len)
        return -1;

    const u8 *pe = (const u8 *)image + dh->e_lfanew;
    const u8 *coff = pe + 4;
    const u8 *opt = coff + sizeof(coff_header_t);

    /* LE signature */
    if (!(pe[0] == 'P' && pe[1] == 'E' && pe[2] == 0 && pe[3] == 0))
        return -1;

    const coff_header_t *ch = (const coff_header_t *)coff;
    if (ch->SizeOfOptionalHeader < sizeof(optional_header_t))
        return -1;

    const optional_header_t *oh = (const optional_header_t *)opt;
    info->dos = dh;
    info->coff = ch;
    info->opt = oh;
    info->entry_rva = oh->AddressOfEntryPoint;
    info->section_count = ch->NumberOfSections;
    info->sections = (const void *)((const u8 *)opt + oh->SizeOfHeaders); // pseudo
    printk("[pe ] machine=0x%04x sections=%u entry=0x%08x base=0x%llx\n",
        ch->Machine, ch->NumberOfSections, oh->AddressOfEntryPoint, oh->ImageBase);
    return 0;
}

int pe_load(const void *image, u64 len, u64 entry_out)
{
    struct pe_image info;
    if (pe_parse(image, len, &info) != 0)
        return -1;
    /* Strategy 2 only stubs; no real load yet */
    printk("[pe ] (stub) entry RVA 0x%08x not mapped yet\n", info.entry_rva);
    (void)entry_out;
    return -1;
}