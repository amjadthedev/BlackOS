/* =============================================================================
*  Black OS - pe.h : Windows PE/COFF loader foundation (Strategy 2)
* ========================================================================== */
#ifndef BLACKOS_PE_H
#define BLACKOS_PE_H

#include "types.h"

#pragma pack(push,1)
typedef struct {
    u8   e_magic[2];   /* MZ */
    u8   e_cblp;
    u8   e_cp;
    u16  e_crlc;
    u16  e_cparhdr;
    u16  e_minalloc;
    u16  e_maxalloc;
    u16  e_ss;
    u16  e_sp;
    u16  e_csum;
    u16  e_ip;
    u16  e_cs;
    u16  e_lfarlc;
    u16  e_ovno;
    u16  e_res1[4];
    u16  e_oemid;
    u16  e_oeminfo;
    u16  e_res2[10];
    u32  e_lfanew;     /* offset to PE header */
} dos_header_t;

typedef struct {
    u8  Signature[4]; /* PE\0\0 */
    u16 Machine;
    u16 NumberOfSections;
    u32 TimeDateStamp;
    u32 PointerToSymbolTable;
    u32 NumberOfSymbols;
    u16 SizeOfOptionalHeader;
    u16 Characteristics;
} coff_header_t;

typedef struct {
    u16 Magic;           /* 0x010B (PE32) or 0x020B (PE32+) */
    u8  MajorLinkerVersion;
    u8  MinorLinkerVersion;
    u32 SizeOfCode;
    u32 SizeOfInitializedData;
    u32 SizeOfUninitializedData;
    u32 AddressOfEntryPoint;
    u32 BaseOfCode;
    u32 BaseOfData;   /* PE32 only */
    u64 ImageBase;
    u32 SectionAlignment;
    u32 FileAlignment;
    u16 MajorOperatingSystemVersion;
    u16 MinorOperatingSystemVersion;
    u16 MajorImageVersion;
    u16 MinorImageVersion;
    u16 MajorSubsystemVersion;
    u16 MinorSubsystemVersion;
    u32 Win32VersionValue;
    u32 SizeOfImage;
    u32 SizeOfHeaders;
    u32 CheckSum;
    u16 Subsystem;
    u16 DllCharacteristics;
    u64 SizeOfStackReserve;
    u64 SizeOfStackCommit;
    u64 SizeOfHeapReserve;
    u64 SizeOfHeapCommit;
    u32 LoaderFlags;
    u32 NumberOfRvaAndSizes;
} optional_header_t;

#pragma pack(pop)

struct pe_image {
    const void *base;
    const dos_header_t  *dos;
    const coff_header_t *coff;
    const optional_header_t *opt;
    u32     entry_rva;
    u16     section_count;
    const void *sections;
};

int  pe_parse(const void *image, u64 len, struct pe_image *info);
int  pe_load(const void *image, u64 len, u64 entry_out);  /* currently logs only */

#endif /* BLACKOS_PE_H */