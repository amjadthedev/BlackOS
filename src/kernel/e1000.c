/* =============================================================================
 *  Black OS - Phase 12: Intel e1000 NIC (QEMU 82540EM) descriptor-ring driver
 *
 *  Poll-mode: no NIC interrupts are armed; the netstack pump drains the RX
 *  ring from the GUI idle loop.  Rings + frame buffers live in identity-
 *  mapped RAM so bus addresses equal virtual addresses (QEMU DMA requirement,
 *  satisfied because everything lives below the 1 GiB identity window).
 * ========================================================================== */
#include "e1000.h"
#include "pci.h"
#include "pmm.h"
#include "paging.h"
#include "string.h"
#include "printk.h"
#include "io.h"

/* ── register offsets ─────────────────────────────────────────────────── */
#define E1000_CTRL      0x0000
#define E1000_STATUS    0x0008
#define E1000_RCTL      0x0100
#define E1000_TCTL      0x0400
#define E1000_RDBAL     0x2800
#define E1000_RDBAH     0x2804
#define E1000_RDLEN     0x2808
#define E1000_RDH       0x2810
#define E1000_RDT       0x2818
#define E1000_TDBAL     0x3800
#define E1000_TDBAH     0x3804
#define E1000_TDLEN     0x3808
#define E1000_TDH       0x3810
#define E1000_TDT       0x3818
#define E1000_RAL       0x5400
#define E1000_RAH       0x5404
#define E1000_IMS       0x00D0
#define E1000_MTA       0x5200

#define RCTL_EN         0x00000002u
#define RCTL_SBP        0x00000004u
#define RCTL_UPE        0x00000008u
#define RCTL_MPE        0x00000010u
#define RCTL_BAM        0x00008000u

#define TCTL_EN         0x00000002u
#define TCTL_PSP        0x00000008u
#define TCTL_CT_ETHER   (0x10u << 4)
#define TCTL_COLD_ADMIN (0x40u << 12)

#define TXD_CMD_EOP     0x01
#define TXD_CMD_IFCS    0x02
#define TXD_CMD_RS      0x04

#define RXD_STAT_DD     0x01
#define RAH_AV          0x80000000u

#define E1000_TXD_COUNT 8
#define E1000_RXD_COUNT 32

#pragma pack(push, 1)
struct e1000_txd {
    u64 addr;
    u16 length;
    u8  cso;
    u8  cmd;
    u8  status;
    u8  css;
    u16 special;
};

struct e1000_rxd {
    u64 addr;
    u16 length;
    u16 csum;
    u8  status;
    u8  errors;
    u16 special;
};
#pragma pack(pop)

static volatile u32 *regs;              /* MMIO base, word-addressed         */
static u32           mmio_phys;
static u8            mac[ETH_MAC_LEN];
static int           present;

static struct e1000_txd txd_ring[E1000_TXD_COUNT] __attribute__((aligned(16)));
static struct e1000_rxd rxd_ring[E1000_RXD_COUNT] __attribute__((aligned(16)));
static u32             *tx_frames[E1000_TXD_COUNT];
static u32             *rx_frames[E1000_RXD_COUNT];

static volatile u32 tx_tail;            /* next desc to fill                 */
static volatile u32 rx_next;            /* next desc the driver reads        */

static u32 rd32(u32 off)      { return regs[off / 4]; }
static void  wr32(u32 off, u32 v) { regs[off / 4] = v; }

void e1000_init(void)
{
    struct pci_dev dev;
    if (pci_scan_nic(&dev) != 0) {
        printk("[net ] no PCI NIC present\n");
        present = 0;
        return;
    }
    if (dev.bar0_abs == 0) {
        printk("[net ] NIC %04x:%04x has no MMIO BAR\n", dev.vendor, dev.device);
        present = 0;
        return;
    }

    mmio_phys = dev.bar0_abs;
    for (u64 p = mmio_phys & ~0x1FFFFFULL; p < mmio_phys + 0x20000; p += 0x200000)
        paging_map_2mb(p);

    regs = (volatile u32 *)(usize)mmio_phys;

    /* frame buffers: one 4 KiB frame each, identity RAM                   */
    for (int i = 0; i < E1000_TXD_COUNT; i++) {
        tx_frames[i] = (u32 *)(usize)pmm_alloc_run(1);
        if (tx_frames[i] == NULL) {
            present = 0;
            return;
        }
        memset(tx_frames[i], 0, PMM_PAGE_SIZE);
        txd_ring[i].status = 0;
    }
    for (int i = 0; i < E1000_RXD_COUNT; i++) {
        rx_frames[i] = (u32 *)(usize)pmm_alloc_run(1);
        if (rx_frames[i] == NULL) {
            present = 0;
            return;
        }
        memset(rx_frames[i], 0, PMM_PAGE_SIZE);
        rxd_ring[i].addr   = (u64)(usize)rx_frames[i];
        rxd_ring[i].status = 0;
    }

    /* controller bring-up                                                 */
    wr32(E1000_IMS, 0);                  /* poll mode: silence interrupts    */
    wr32(E1000_CTRL, rd32(E1000_CTRL) | (1u << 6));   /* SLU: link up         */

    /* TX ring                                                              */
    wr32(E1000_TDBAL, (u32)(usize)txd_ring);
    wr32(E1000_TDBAH, 0);
    wr32(E1000_TDLEN, (u32)(sizeof(txd_ring)));
    wr32(E1000_TDH,   0);
    wr32(E1000_TDT,   0);
    wr32(E1000_TCTL,  TCTL_EN | TCTL_PSP | TCTL_CT_ETHER | TCTL_COLD_ADMIN);

    /* RX ring                                                              */
    wr32(E1000_RDBAL, (u32)(usize)rxd_ring);
    wr32(E1000_RDBAH, 0);
    wr32(E1000_RDLEN, (u32)(sizeof(rxd_ring)));
    wr32(E1000_RDH,   0);
    wr32(E1000_RDT,   E1000_RXD_COUNT - 1);
    wr32(E1000_RCTL,  RCTL_EN | RCTL_UPE | RCTL_MPE | RCTL_BAM | (0u)); /* 2048 */

    /* unicast address filter: 52:54:00:12:34:56 (QEMU-friendly static)     */
    mac[0] = 0x52; mac[1] = 0x54; mac[2] = 0x00;
    mac[3] = 0x12; mac[4] = 0x34; mac[5] = 0x56;

    u32 ral = (u32)mac[0] | ((u32)mac[1] << 8) | ((u32)mac[2] << 16) | ((u32)mac[3] << 24);
    u32 rah = (u32)mac[4] | ((u32)mac[5] << 8) | RAH_AV;
    wr32(E1000_RAL, ral);
    wr32(E1000_RAH, rah);

    tx_tail = 0;
    rx_next = 0;
    present = 1;

    printk("[net ] e1000 up @ 0x%x (vendor %04x device %04x)\n",
            mmio_phys, dev.vendor, dev.device);
}

int  e1000_present(void) { return present; }

void e1000_get_mac(u8 out[ETH_MAC_LEN])
{
    for (int i = 0; i < ETH_MAC_LEN; i++)
        out[i] = mac[i];
}

int e1000_send(const void *frame, u16 len)
{
    if (!present || len == 0)
        return -1;
    if (len > 4096)
        return -1;

    if (tx_tail >= E1000_TXD_COUNT)
        tx_tail = 0;

    /* wait for the current descriptor back (bounded spin)                 */
    u32 spins = 0;
    while (txd_ring[tx_tail].status == 0 && spins < 1000000)
        spins++;
    txd_ring[tx_tail].status = 0;

    memcpy(tx_frames[tx_tail], frame, len);
    txd_ring[tx_tail].addr   = (u64)(usize)tx_frames[tx_tail];
    txd_ring[tx_tail].length = len;
    txd_ring[tx_tail].cmd    = TXD_CMD_EOP | TXD_CMD_IFCS | TXD_CMD_RS;
    txd_ring[tx_tail].cso    = 0;
    txd_ring[tx_tail].css    = 0;
    txd_ring[tx_tail].special = 0;

    wr32(E1000_TDT, (u32)((tx_tail + 1) % E1000_TXD_COUNT));
    tx_tail = (tx_tail + 1) % E1000_TXD_COUNT;
    return 0;
}

u16 e1000_recv(void *frame, u16 cap)
{
    if (!present || frame == NULL)
        return 0;
    if (rx_next >= E1000_RXD_COUNT)
        rx_next = 0;

    if (!(rxd_ring[rx_next].status & RXD_STAT_DD))
        return 0;                        /* ring drain caught up             */

    u16 len = rxd_ring[rx_next].length;
    u16 take = (len <= cap) ? len : cap;

    memcpy(frame, rx_frames[rx_next], take);
    rxd_ring[rx_next].status = 0;        /* hand the buffer back             */

    /* tell the NIC this descriptor is free again (RDT = last processed)   */
    wr32(E1000_RDT, (u32)rx_next);

    rx_next = (rx_next + 1) % E1000_RXD_COUNT;
    return take;
}
