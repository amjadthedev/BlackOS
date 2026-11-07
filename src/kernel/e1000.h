/* =============================================================================
 *  Black OS - Phase 12: Intel e1000 NIC (QEMU 82540EM) descriptor-ring driver
 *  Poll-mode TX/RX rings mapped through MMIO; hands Ethernet frames to the
 *  netstack receive pump.
 * ========================================================================== */
#ifndef BLACKOS_E1000_H
#define BLACKOS_E1000_H

#include "types.h"

#define ETH_MAC_LEN     6
#define ETH_FRAME_MAX   1518

void e1000_init(void);                          /* PCI probe + rings up    */
int  e1000_present(void);
void e1000_get_mac(u8 out[ETH_MAC_LEN]);

/* Transmit one complete Ethernet frame (header + payload, single buffer).
 * Returns 0 on success, -1 when the NIC is absent or TX ring is jammed.   */
int  e1000_send(const void *frame, u16 len);

/* Drain at most one received frame into `frame` (max `cap` bytes).
 * Returns the received length, 0 when the RX ring is empty.               */
u16  e1000_recv(void *frame, u16 cap);

#endif /* BLACKOS_E1000_H */
