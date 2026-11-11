/* =============================================================================
 *  Black OS - acpi.c : minimal ACPI power-off
 *
 *  Finds RSDP (EBDA word @ 0x40E, else scan of 0xE0000..0xFFFFF), validates
 *  checksums, walks RSDT/XSDT for the FADT ("FACP") and scrapes _S5_ from
 *  the DSDT.  acpi_poweroff() enables ACPI if the BIOS hasn't (SMI_CMD),
 *  then writes SLP_EN|SLP_TYP to PM1a_CNT (and PM1b_CNT when declared).
 *
 *  Extended (64-bit) FADT fields win; otherwise the legacy 32-bit port
 *  fields are used, exactly per the ACPI spec.
 * ========================================================================== */
#include "types.h"
#include "acpi.h"
#include "io.h"
#include "printk.h"
#include "string.h"

/* ------------------------------------------------------------- structures -- */
struct rsdp_desc {
    char sig[8];
    u8   checksum;
    char oem_id[6];
    u8   revision;
    u32  rsdt_address;
} __attribute__((packed));
struct rsdp_rev2 {
    struct rsdp_desc first;
    u32  length;
    u64  xsdt_address;
    u8   checksum_extended;
    u8   reserved[3];
} __attribute__((packed));
struct sdt_header {
    char signature[4];
    u32  length;
    u8   revision;
    u8   checksum;
    char oem_id[6];
    char oem_table_id[8];
    u32  oem_revision;
    u32  creator_id;
    u32  creator_revision;
} __attribute__((packed));
struct aml_gas {                            /* 12-byte GAS register block     */
    u8   addr_space;                        /* 1 == I/O Port                   */
    u8   reg_bit_width;
    u8   reg_bit_offset;
    u8   access_size;
    u64  address;
} __attribute__((packed));
struct fadt {
    struct sdt_header h;
    u32  firmware_ctrl;
    u32  dsdt;
    u8   reserved;
    u8   preferred_pm_profile;
    u16  sci_int;
    u32  smi_cmd;
    u8   acpi_enable;
    u8   acpi_disable;
    u8   s4bios_req;
    u8   pstate_cnt;
    u32  pm1a_evt_blk;
    u32  pm1b_evt_blk;
    u32  pm1a_cnt_blk;
    u32  pm1b_cnt_blk;
    u32  pm2_cnt_blk;
    u32  pm_tmr_blk;
    u32  gp_e0_blk;
    u32  gp_e1_blk;
    u8   pm1_evt_len;
    u8   pm1_cnt_len;
    u8   pm2_cnt_len;
    u8   pm_tmr_len;
    u8   gp_e0_len;
    u8   gp_e1_len;
    u8   gp1_base;
    u8   cst_cnt;
    u16  p_lvl2_lat;
    u16  p_lvl3_lat;
    u16  flush_size;
    u16  flush_stride;
    u8   duty_offset;
    u8   duty_width;
    u8   day_alarm;
    u8   mon_alarm;
    u8   century;
    u16  iapc_boot_arch;
    u8   res2;
    u32  flags;
    struct aml_gas reset_reg;               /* offset 116                    */
    u8   reset_value;                       /* offset 128                    */
    u16  arm_boot_arch;                     /* offset 129                    */
    u8   fadt_minor_version;                /* offset 131                    */
    u64  x_firmware_ctrl;                    /* offset 132                    */
    u64  x_dsdt;                            /* offset 140                    */
    struct aml_gas x_pm1a_evt;              /* 148                           */
    struct aml_gas x_pm1b_evt;              /* 160                           */
    struct aml_gas x_pm1a_cnt;              /* 172                           */
    struct aml_gas x_pm1b_cnt;              /* 184                           */
} __attribute__((packed));

/* ----------------------------------------------------------------- globals -- */
static u64 g_rsdp_phys;
static u8  g_rsdp_rev;                      /* 0 = ACPI 1.0, >=2 = 2.0+       */
static u64 g_rsdt_phys;
static int g_xsdt;                          /* 1 when XSDT (vs 0 == RSDT)     */
static u64 g_fadt_phys;
static u64 g_dsdt_phys;
static u32 g_smi_cmd;
static u8  g_acpi_enable;
static u16 g_pm1a_port;                     /* 0 == absent                    */
static u16 g_pm1b_port;
u8  g_acpi_slp_typ;                         /* parsed from _S5_ (public)      */
u8  g_acpi_pmf;
u8  g_acpi_sci_int;

/* --------------------------------------------------------------- helpers -- */
static u8 sdt_checksum(const u8 *p, u32 len)
{
    u32 sum = 0;
    for (u32 i = 0; i < len; i++)
        sum += p[i];
    return (u8)(sum & 0xff);
}

static int rsdp_valid(const u8 *p)
{
    const struct rsdp_desc *r = (const struct rsdp_desc *)p;
    if (memcmp(r->sig, "RSD PTR ", 8) != 0)
        return 0;
    if (sdt_checksum(p, 20) != 0)
        return 0;
    if (r->revision >= 2) {
        const struct rsdp_rev2 *r2 = (const struct rsdp_rev2 *)p;
        if (sdt_checksum(p, sizeof(*r2)) != 0)
            return 0;
    }
    return 1;
}
static int find_rsdp(u64 start, u64 end)
{
    for (u64 i = start; i + 20 <= end; i += 16) {
        const u8 *p = (const u8 *)(usize)i;
        if (memcmp(p, "RSD PTR ", 8) != 0)
            continue;
        if (rsdp_valid(p)) {
            g_rsdp_phys = i;
            const struct rsdp_desc *r = (const struct rsdp_desc *)p;
            g_rsdp_rev  = r->revision;
            return 1;
        }
    }
    return 0;
}

static void walk_tables(void)
{
    const struct sdt_header *h;
    const u8                *p, *end;
    u64                     phys;

    const struct rsdp_desc *r = (const struct rsdp_desc *)(usize)g_rsdp_phys;
    if (g_rsdp_rev >= 2) {
        const struct rsdp_rev2 *r2 = (const struct rsdp_rev2 *)(usize)g_rsdp_phys;
        phys = r2->xsdt_address;
    } else {
        phys = (u64)r->rsdt_address;
    }
    if (!phys)
        return;

    h = (const struct sdt_header *)(usize)phys;
    if (sdt_checksum((const u8 *)h, h->length) != 0) {
        kprintf("[acpi] %s checksum bad\n", g_rsdp_rev >= 2 ? "XSDT" : "RSDT");
        return;
    }
    p   = (const u8 *)h + sizeof(*h);
    end = (const u8 *)h + h->length;
    g_xsdt = (g_rsdp_rev >= 2);

    if (g_xsdt) {
        for (const u64 *e = (const u64 *)p; (const u8 *)(e + 1) <= end; e++)
            if (*e) {
                const struct sdt_header *t = (const struct sdt_header *)(usize)*e;
                if (memcmp(t->signature, "FACP", 4) == 0 &&
                    sdt_checksum((const u8 *)t, t->length) == 0) {
                    g_fadt_phys = *e;
                    return;
                }
            }
    } else {
        for (const u32 *e = (const u32 *)p; (const u8 *)(e + 1) <= end; e++)
            if (*e) {
                const struct sdt_header *t = (const struct sdt_header *)(usize)*e;
                if (memcmp(t->signature, "FACP", 4) == 0 &&
                    sdt_checksum((const u8 *)t, t->length) == 0) {
                    g_fadt_phys = *e;
                    return;
                }
            }
    }
}

static int inspect_fadt(void)
{
    if (!g_fadt_phys)
        return -1;
    const struct fadt *f = (const struct fadt *)(usize)g_fadt_phys;
    if (f->h.length < 116)                     /* minimum FADT size          */
        return -1;
    g_acpi_pmf     = f->preferred_pm_profile;
    g_acpi_sci_int = (u8)(f->sci_int & 0xff);
    g_smi_cmd      = f->smi_cmd;
    g_acpi_enable  = f->acpi_enable;

    /* Prefer the 64-bit GAS registers when declared and system I/O. */
    if (f->h.length >= sizeof(*f)) {
        if (f->x_pm1a_cnt.addr_space == 1)     /* AML_SYSTEM_IO              */
            g_pm1a_port = (u16)f->x_pm1a_cnt.address;
        if (f->x_pm1b_cnt.addr_space == 1)
            g_pm1b_port = (u16)f->x_pm1b_cnt.address;
        g_dsdt_phys = f->x_dsdt;
    }
    if (!g_pm1a_port && f->pm1a_cnt_blk)
        g_pm1a_port = (u16)f->pm1a_cnt_blk;
    if (!g_pm1b_port && f->pm1b_cnt_blk)
        g_pm1b_port = (u16)f->pm1b_cnt_blk;
    if (!g_dsdt_phys)
        g_dsdt_phys = (u64)f->dsdt;
    return 0;
}

static void parse_s5(const u8 *table, u64 len)
{
    u64 i = 0;
    while (i + 4 < len) {
        if (memcmp(table + i, "_S5_", 4) != 0) { i++; continue; }
        u64 j = i;
        for (u64 n = 0; n < 96 && j < len; j++, n++) {
            if (table[j] != 0x12)              /* PackageOp                  */
                continue;
            u64 k = j + 1;
            if (k + 2 >= len) return;
            (void)table[k++];                  /* PkgLength                  */
            u64 n_el = table[k++];             /* NumElements                */
            if (n_el < 1) return;
            if (table[k] == 0x0A) k++;         /* BytePrefix on element 0    */
            u8 a = (k < len) ? table[k++] : 0;
            if (table[k] == 0x0A) k++;         /* BytePrefix on element 1    */
            g_acpi_slp_typ = a;
            return;
        }
        i = j;
    }
}

void acpi_report(void)
{
    kprintf("[acpi] RSDP=0x%llx rev=%s rev\n", g_rsdp_phys,
            g_rsdp_rev >= 2 ? "2.0" : "1.0");
    kprintf("[acpi] FADT=0x%llx DSDT=0x%llx\n", g_fadt_phys, g_dsdt_phys);
    kprintf("[acpi] PM1a=0x%04x PM1b=0x%04x SMI=0x%x ACPI_EN=0x%02x\n",
            g_pm1a_port, g_pm1b_port, g_smi_cmd, g_acpi_enable);
    kprintf("[acpi] pmf(mask)=%u sci_int=%u slp_typ=%u\n",
            g_acpi_pmf, g_acpi_sci_int, g_acpi_slp_typ);
}

void acpi_poweroff(void)
{
    /* Enable ACPI on ancient BIOSes still in PIC-only mode (rare). */
    if (g_smi_cmd && g_acpi_enable)
        outb((u16)g_smi_cmd, (u8)g_acpi_enable);

    if (g_dsdt_phys) {
        const struct sdt_header *h = (const struct sdt_header *)(usize)g_dsdt_phys;
        if (sdt_checksum((const u8 *)h, h->length) == 0 && h->length >= 4)
            parse_s5((const u8 *)h + sizeof(*h), h->length - sizeof(*h));
    }

    u16 ctl = (u16)(0x2000 | ((u16)g_acpi_slp_typ & 0x7) << 10);   /* SLP_EN|S5 */
    if (g_pm1a_port) { outw(g_pm1a_port, ctl); io_wait(); }
    if (g_pm1b_port) { outw(g_pm1b_port, ctl); io_wait(); }

    /* Last-resort QEMU legacy ports (keeps headless VM behavior). */
    if (!g_pm1a_port) {
        outw(0x604, 0x2000);
        outw(0xb004, 0x2000);
    }
}

void acpi_reboot(void)
{
    io_wait();
    outb(0x64, 0xfe);                        /* 8042 reset pulse             */
}

void acpi_init(void)
{
    u16 ebda = *(volatile u16 *)(usize)0x0000040e;
    if (ebda) {
        u64 ebda_addr = (u64)ebda << 4;
        find_rsdp(ebda_addr, ebda_addr + 1024);
    }
    if (!g_rsdp_phys)
        find_rsdp(0x000e0000, 0x00100000);
    if (!g_rsdp_phys)
        /* UEFI firmware (OVMF / Hyper-V Gen2): ACPI tables are allocated
         * from RAM, not the legacy ROM window.  Scan the first 1 GiB. */
        find_rsdp(0x00100000, 0x40000000);
    if (!g_rsdp_phys) {
        kprintf("[acpi] RSDP not found\n");
        return;
    }
    walk_tables();
    if (inspect_fadt() != 0) {
        kprintf("[acpi] no (valid) FADT\n");
        return;
    }
    kprintf("[acpi] FADT at 0x%llx, DSDT at 0x%llx (pmf %u, sci %u)\n",
            g_fadt_phys, g_dsdt_phys, g_acpi_pmf, g_acpi_sci_int);
}
