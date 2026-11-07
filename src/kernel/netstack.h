/* =============================================================================
 *  Black OS - Phase 12: IPv4 network stack (ARP / IP / ICMP / UDP / TCP-lite)
 *
 *  Built directly over the e1000 descriptor rings; hands HTTP/JSON payloads
 *  to weather.c.  Target network: QEMU slirp (10.0.2.15/24, gw/DNS 10.0.2.2).
 * ========================================================================== */
#ifndef BLACKOS_NETSTACK_H
#define BLACKOS_NETSTACK_H

#include "types.h"

#define NET_PROTO_ICMP   1
#define NET_PROTO_TCP    6
#define NET_PROTO_UDP    17

/* ── layer state ─────────────────────────────────────────────────────────  */
struct net_state {
    u8  mac[6];
    u8  ip[4];
    u8  gw[4];
    u8  dns[4];
    u8  mask[4];
    u32 dhcp_state;                     /* 0 down, 1 discover, 2 bound       */
};

extern struct net_state net;

void   net_init(void);
void   net_poll(void);                  /* pump RX + DHCP timer (call often) */

/* ── raw frame / ARP / IP ─────────────────────────────────────────────────  */
int  net_arp_resolve(const u8 proto_ip[4], u8 out_mac[6]);  /* 0 found        */
void net_ip_send(const u8 dest_ip[4], u8 proto, const void *payload, u16 len);

/* ── UDP ─────────────────────────────────────────────────────────────────  */
int  net_udp_send(const u8 dest_ip[4], u16 sport, u16 dport,
                  const void *payload, u16 len);

/* ── DHCP (bounded state machine driven by net_poll) ─────────────────────  */
int  net_dhcp_bound(void);              /* 1 once address is acquired        */

/* ── DNS (single outstanding query) ───────────────────────────────────────  */
int  net_dns_lookup(const char *host, u8 out_ip[4]);       /* 0 on success   */

/* ── TCP-lite connections ─────────────────────────────────────────────────  */
#define NET_TCP_CLOSED     0
#define NET_TCP_SYN_SENT   1
#define NET_TCP_ESTABLISHED 2
#define NET_TCP_FIN_SENT   3
#define NET_TCP_DEAD       4

typedef struct net_tcp *net_tcp_h;

net_tcp_h net_tcp_open(const u8 dest_ip[4], u16 dest_port);
int  net_tcp_send(net_tcp_h c, const void *data, u32 len);
int  net_tcp_recv(net_tcp_h c, void *buf, u32 cap);        /* >=1 bytes or 0 */
int  net_tcp_close(net_tcp_h c);                           /* 0 when drained */

/* ── ICMP ─────────────────────────────────────────────────────────────────  */
int  net_ping(const u8 dest_ip[4]);     /* one echo + bounded wait           */

/* ── HTTP 1.1 (single request per connection) ─────────────────────────────  */
net_tcp_h net_http_get(const char *host, const char *path);   /* NULL fail  */
int  net_http_read(net_tcp_h c, void *buf, u32 cap);
void net_http_close(net_tcp_h c);

/* ── firewall ─────────────────────────────────────────────────────────────  */
#define FW_ALLOW   0
#define FW_DENY    1
#define FW_DIR_IN  0
#define FW_DIR_OUT 1

int  net_firewall(int dir, u8 proto, const u8 remote_ip[4], u16 port);
void net_firewall_add(int dir, int permit, u8 proto, const u8 remote_ip[4],
                      u8 mask_bits, u16 port);
void net_firewall_list(void);           /* serial dump                       */

/* ── JSON helpers (weather payload decoding) ─────────────────────────────  */
const char *net_json_field_str(const char *json, const char *key,
                               char *out, u32 cap);
int  net_json_field_num(const char *json, const char *key, int *out);

#endif /* BLACKOS_NETSTACK_H */
