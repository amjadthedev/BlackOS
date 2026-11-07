/* =============================================================================
 *  Black OS - Phase 12: PCI configuration space (bus/device/function probe)
 * ========================================================================== */
#include "pci.h"
#include "io.h"
#include "printk.h"

u32 pci_cfg_read32(u8 bus, u8 dev, u8 fn, u8 offset)
{
    u32 addr = 0x80000000u
             | ((u32)bus << 16)
             | ((u32)dev << 11)
             | ((u32)fn  << 8)
             | ((u32)offset & 0xFCu);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

void pci_cfg_write32(u8 bus, u8 dev, u8 fn, u8 offset, u32 value)
{
    u32 addr = 0x80000000u
             | ((u32)bus << 16)
             | ((u32)dev << 11)
             | ((u32)fn  << 8)
             | ((u32)offset & 0xFCu);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, value);
}

/* Inspect one (bus, dev, fn) slot.  Returns 0 if a live device was found. */
static int pci_probe_slot(u8 bus, u8 dev, u8 fn, struct pci_dev *out)
{
    u32 id = pci_cfg_read32(bus, dev, fn, 0x00);
    u16 vendor = (u16)(id & 0xFFFF);

    if (vendor == 0xFFFF || vendor == 0)
        return -1;                       /* empty slot                      */

    u32 class_rev = pci_cfg_read32(bus, dev, fn, 0x08);
    u32 bar0      = pci_cfg_read32(bus, dev, fn, 0x10);

    out->vendor     = vendor;
    out->device     = (u16)(id >> 16);
    out->bus        = bus;
    out->dev        = dev;
    out->fn         = fn;
    out->class_code = (u8)((class_rev >> 24) & 0xFF);
    out->bar0_info  = bar0;

    if ((bar0 & 0x1) == 0) {             /* memory-mapped BAR               */
        u32 abs = bar0 & 0xFFFFFFF0u;
        out->bar0_abs = abs;
    } else {
        out->bar0_abs = 0;               /* I/O port BAR: not supported     */
    }
    return 0;
}

int pci_scan_nic(struct pci_dev *out)
{
    return pci_scan_class(0x02, out);
}

/* Scan for a device by base class code (e.g. 0x04 = multimedia).          */
int pci_scan_class(u8 class_code, struct pci_dev *out)
{
    struct pci_dev d;

    for (u32 bus = 0; bus <= 2; bus++) {
        for (u32 dev = 0; dev < 32; dev++) {
            if (pci_probe_slot((u8)bus, (u8)dev, 0, &d) != 0)
                continue;
            if (d.class_code == class_code) {
                *out = d;
                return 0;
            }
        }
    }
    return -1;
}
