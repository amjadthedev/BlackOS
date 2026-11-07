/* =============================================================================
 *  Black OS - Phase 12: PCI configuration space (bus/device/function probe)
 * ========================================================================== */
#ifndef BLACKOS_PCI_H
#define BLACKOS_PCI_H

#include "types.h"

#define PCI_CONFIG_ADDR   0xCF8
#define PCI_CONFIG_DATA   0xCFC

struct pci_dev {
    u16 vendor;
    u16 device;
    u8  bus, dev, fn;
    u8  class_code;                     /* 02 = network controller           */
    u32 bar0_info;                      /* raw BAR0 word                     */
    u32 bar0_abs;                       /* physical MMIO base (0 = absent)   */
};

/* Scan known bus/device ranges for an NIC-class device.  Returns the
 * description in *out and 0 on success, -1 when nothing matched.          */
int  pci_scan_nic(struct pci_dev *out);

/* Scan for a device by base class code (e.g. 0x04 = multimedia).          */
int  pci_scan_class(u8 class_code, struct pci_dev *out);

/* Generic config accessors (32-bit slots only, offset must be 4-aligned). */
u32  pci_cfg_read32(u8 bus, u8 dev, u8 fn, u8 offset);
void pci_cfg_write32(u8 bus, u8 dev, u8 fn, u8 offset, u32 value);

#endif /* BLACKOS_PCI_H */
