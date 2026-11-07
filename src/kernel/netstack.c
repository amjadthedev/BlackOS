/* =============================================================================
 *  Black OS - Phase 12: IPv4 network stack
 *
 *  Layering (bottom-up), all poll-driven inside net_poll():
 *
 *      e1000 (frames)  ->  ARP responder/requester
 *                      ->  IP demux { ICMP, UDP, TCP }
 *      DHCP  (UDP 68)  ->  address, router, DNS acquisition
 *      DNS   (UDP 53)  ->  hostname -> A record
 *      TCP-lite        ->  SYN handshake, streaming, FIN (enough for HTTP)
 *      HTTP 1.1        ->  GET / read
 *      firewall        ->  inbound/outbound rule check
 *
 *  Wire-format discipline: every multi-byte protocol field goes through
 *  htons()/htonl() on transmit and ntohs()/ntohl() on receive.  Target
 *  network is QEMU slirp (10.0.2.15/24, gw + DNS 10.0.2.2).
 * ========================================================================== */
#include "netstack.h"
#include "e1000.h"
#include "string.h"
#include "printk.h"
#include "idt.h"

/* ── QEMU slirp defaults (overridden by DHCP whenever it completes) ──────  */
static const u8 SLIRP_GW[4]    = { 10, 0, 2, 2 };
static const u8 SLIRP_DNS[4]   = { 10, 0, 2, 2 };
static const u8 SLIRP_MASK[4]  = { 255, 255, 255, 0 };
static const u8 OUR_IP[4]      = { 10, 0, 2, 15 };

struct net_state net;

/* ── byte order helpers ─────────────────────────────────────────────────  */
static u16 htons16(u16 v)  { return (u16)((v << 8) | (v >> 8)); }
static u16 ntohs16(u16 v)  { return htons16(v); }
static u32 htonl32(u32 v)
{
    return ((v & 0xFFu) << 24) | ((v & 0xFF00u) << 8)
         | ((v & 0xFF0000u) >> 8) | ((v & 0xFF000000u) >> 24);
}
static u32 ntohl32(u32 v)  { return htonl32(v); }

static void net_poll_internal(void);       /* pump RX (defined below)       */

#pragma pack(push, 1)
struct eth_hdr {
    u8  dst[6];
    u8  src[6];
    u16 type;
};

struct arp_hdr {
    u16 htype;
    u16 ptype;
    u8  hlen;
    u8  plen;
    u16 oper;
    u8  sha[6];
    u8  spa[4];
    u8  tha[6];
    u8  tpa[4];
};

struct ip4_hdr {
    u8  ver_ihl;
    u8  tos;
    u16 total_len;
    u16 id;
    u16 flags_frag;
    u8  ttl;
    u8  proto;
    u16 checksum;
    u8  src[4];
    u8  dst[4];
};

struct udp_hdr {
    u16 sport;
    u16 dport;
    u16 length;
    u16 checksum;
};

struct tcp_hdr {
    u16 sport;
    u16 dport;
    u32 seq;
    u32 ack;
    u8  data_off;               /* high nibble = header words               */
    u8  flags;
    u16 window;
    u16 checksum;
    u16 urgent;
};

struct icmp_hdr {
    u8  type;
    u8  code;
    u16 checksum;
    u16 id;
    u16 seq;
};

struct dhcp_hdr {
    u8  op, htype, hlen, hops;
    u32 xid;
    u16 secs, flags;
    u8  ciaddr[4], yiaddr[4], siaddr[4], giaddr[4];
    u8  chaddr[16];
    u8  sname[64];
    u8  file[128];
    u32 magic;
};

struct dns_hdr {
    u16 id, flags, qd, an, ns, ar;
};
#pragma pack(pop)

#define ETH_TYPE_ARP        0x0806
#define ETH_TYPE_IPV4       0x0800
#define DHCP_MAGIC_HOST     0x63825363u      /* host order; wire is BE      */
#define DHCP_DISCOVER       1
#define DHCP_OFFER          2
#define DHCP_REQUEST        3
#define DHCP_ACK            5
#define TCP_FIN             0x01
#define TCP_SYN             0x02
#define TCP_RST             0x04
#define TCP_PSH             0x08
#define TCP_ACK             0x10

/* ───────────────────────────── checksums ───────────────────────────────── */
static u16 inet_checksum(const void *data, u32 len)
{
    const u8 *p = (const u8 *)data;
    u32 sum = 0;

    for (u32 i = 0; i + 1 < len; i += 2)
        sum += ((u32)p[i] << 8) | p[i + 1];
    if (len & 1)
        sum += (u32)p[len - 1] << 8;

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (u16)~sum;
}

static u16 proto_checksum(const u8 src[4], const u8 dst[4], u8 proto,
                          const void *seg, u32 len)
{
    static u8 scratch[2048 + 12];
    if (len + 12 > sizeof(scratch))
        return 0xFFFF;

    u8 *s = scratch;
    memcpy(s, src, 4);      s += 4;
    memcpy(s, dst, 4);      s += 4;
    *s++ = 0;
    *s++ = proto;
    *s++ = (u8)(len >> 8);
    *s++ = (u8)(len & 0xFF);
    memcpy(s, seg, len);

    return inet_checksum(scratch, len + 12);
}

/* ─────────────────────────────── ARP cache ─────────────────────────────── */
#define ARP_CACHE_MAX 8
struct arp_entry {
    u8  ip[4];
    u8  mac[6];
    u32 seen_ms;
};
static struct arp_entry arp_cache[ARP_CACHE_MAX];

static void arp_cache_put(const u8 ip[4], const u8 mac[6])
{
    for (int i = 0; i < ARP_CACHE_MAX; i++) {
        if (arp_cache[i].ip[0] != 0 && memcmp(arp_cache[i].ip, ip, 4) == 0) {
            memcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].seen_ms = (u32)uptime_ms;
            return;
        }
    }
    for (int i = 0; i < ARP_CACHE_MAX; i++) {
        if (arp_cache[i].ip[0] == 0 ||
            uptime_ms - arp_cache[i].seen_ms > 60000) {
            memcpy(arp_cache[i].ip, ip, 4);
            memcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].seen_ms = (u32)uptime_ms;
            return;
        }
    }
}

static void mac_copy(u8 *dst, const u8 *src) { memcpy(dst, src, 6); }

static int ip_eq(const u8 a[4], const u8 b[4]) { return memcmp(a, b, 4) == 0; }

int net_arp_resolve(const u8 proto_ip[4], u8 out_mac[6])
{
    for (int i = 0; i < ARP_CACHE_MAX; i++)
        if (arp_cache[i].ip[0] != 0 && memcmp(arp_cache[i].ip, proto_ip, 4) == 0) {
            mac_copy(out_mac, arp_cache[i].mac);
            return 0;
        }

    u8 frame[64];
    memset(frame, 0, sizeof(frame));
    struct eth_hdr *eth = (struct eth_hdr *)frame;
    for (int i = 0; i < 6; i++)
        eth->dst[i] = 0xFF;
    e1000_get_mac(eth->src);
    eth->type = htons16(ETH_TYPE_ARP);

    struct arp_hdr *arp = (struct arp_hdr *)(frame + 14);
    arp->htype = htons16(1);
    arp->ptype = htons16(ETH_TYPE_IPV4);
    arp->hlen  = 6;
    arp->plen  = 4;
    arp->oper  = htons16(1);             /* request                          */
    e1000_get_mac(arp->sha);
    memcpy(arp->spa, net.ip, 4);
    for (int i = 0; i < 6; i++)
        arp->tha[i] = 0;
    memcpy(arp->tpa, proto_ip, 4);

    e1000_send(frame, (u16)(14 + sizeof(struct arp_hdr)));

    /* bounded poll until the reply lands (ARP lives on net_poll)          */
    for (u32 spin = 0; spin < 60; spin++) {
        net_poll_internal();

        for (int i = 0; i < ARP_CACHE_MAX; i++)
            if (arp_cache[i].ip[0] != 0 &&
                memcmp(arp_cache[i].ip, proto_ip, 4) == 0) {
                mac_copy(out_mac, arp_cache[i].mac);
                return 0;
            }
        for (volatile int k = 0; k < 2048; k++)
            ;
    }
    return -1;
}

/* ───────────────────────────────── IP send ─────────────────────────────── */
static int ip_frag_id;

static void ip4_transmit(const u8 dest_ip[4], u8 proto,
                         const void *payload, u16 len)
{
    if (e1000_present() == 0)
        return;
    if (len + 20 > 1480)
        return;                          /* single-frame segments only       */

    u8 mac[6];
    if (net_arp_resolve(dest_ip, mac) != 0) {
        /* on-network hosts sometimes ARP unanswered; the gateway is the
         * fallback route — but if the gateway itself is unreachable, drop  */
        if (ip_eq(dest_ip, net.gw) || net_arp_resolve(net.gw, mac) != 0)
            return;
    }

    u8 frame[1536];
    memset(frame, 0, sizeof(frame));
    struct eth_hdr *eth = (struct eth_hdr *)frame;
    mac_copy(eth->dst, mac);
    e1000_get_mac(eth->src);
    eth->type = htons16(ETH_TYPE_IPV4);

    struct ip4_hdr *ip = (struct ip4_hdr *)(frame + 14);
    ip->ver_ihl   = 0x45;
    ip->tos       = 0;
    ip->total_len = htons16((u16)(20 + len));
    ip->id        = htons16((u16)(ip_frag_id++));
    ip->flags_frag= 0;
    ip->ttl       = 64;
    ip->proto     = proto;
    ip->checksum  = 0;
    memcpy(ip->src, net.ip, 4);
    memcpy(ip->dst, dest_ip, 4);
    ip->checksum  = htons16(inet_checksum(ip, 20));

    memcpy(frame + 34, payload, len);
    e1000_send(frame, (u16)(34 + len));
}

void net_ip_send(const u8 dest_ip[4], u8 proto, const void *payload, u16 len)
{
    ip4_transmit(dest_ip, proto, payload, len);
}

/* ───────────────────────────────── UDP ─────────────────────────────────── */
static void udp_transmit(const u8 dest_ip[4], u16 sport, u16 dport,
                         const void *payload, u16 len, u16 csum_zero)
{
    u8 seg[2048];
    if ((u32)len + 8 > sizeof(seg))
        return;
    u16 total = (u16)(8 + len);

    struct udp_hdr *u = (struct udp_hdr *)seg;
    u->sport    = htons16(sport);
    u->dport    = htons16(dport);
    u->length   = htons16(total);
    u->checksum = 0;
    memcpy(seg + 8, payload, len);

    if (csum_zero == 0)
        u->checksum = htons16(proto_checksum(net.ip, dest_ip, NET_PROTO_UDP,
                                             seg, total));

    ip4_transmit(dest_ip, NET_PROTO_UDP, seg, total);
}

int net_udp_send(const u8 dest_ip[4], u16 sport, u16 dport,
                 const void *payload, u16 len)
{
    udp_transmit(dest_ip, sport, dport, payload, len, 0);
    return 0;
}

/* ───────────────────────────────── DHCP ────────────────────────────────── */
static u32 dhcp_xid;
static u32 dhcp_started_ms;
static u8  dhcp_requested[4];

static void dhcp_option_put(u8 *opts, u32 *len, u8 code, const void *data, u32 n)
{
    opts[(*len)++] = code;
    opts[(*len)++] = (u8)n;
    memcpy(opts + *len, data, n);
    *len += n;
}

static void dhcp_send(u8 msg_type)
{
    u8 payload[300];
    memset(payload, 0, sizeof(payload));

    struct dhcp_hdr *d = (struct dhcp_hdr *)payload;
    d->op    = 1;                        /* boot request                     */
    d->htype = 1;
    d->hlen  = 6;
    d->xid   = htonl32(dhcp_xid);
    if (msg_type != DHCP_DISCOVER)
        d->secs = htons16((u16)((uptime_ms - dhcp_started_ms) / 1000));
    memcpy(d->chaddr, net.mac, 6);
    d->magic = htonl32(DHCP_MAGIC_HOST);
    memcpy(d->siaddr, net.gw, 4);
    memcpy(d->giaddr, net.gw, 4);

    u8 opts[32];
    u32 ol = 0;
    dhcp_option_put(opts, &ol, 53, &msg_type, 1);
    if (msg_type == DHCP_REQUEST)
        dhcp_option_put(opts, &ol, 50, dhcp_requested, 4);
    opts[ol++] = 255;                    /* end                              */

    memcpy(payload + sizeof(struct dhcp_hdr), opts, ol);
    u16 plen = (u16)(sizeof(struct dhcp_hdr) + ol);

    /* DHCP is broadcast at both layers                                    */
    u8 frame[512];
    memset(frame, 0, sizeof(frame));
    struct eth_hdr *eth = (struct eth_hdr *)frame;
    for (int i = 0; i < 6; i++)
        eth->dst[i] = 0xFF;
    e1000_get_mac(eth->src);
    eth->type = htons16(ETH_TYPE_IPV4);

    struct ip4_hdr *ip = (struct ip4_hdr *)(frame + 14);
    ip->ver_ihl   = 0x45;
    ip->ttl       = 128;
    ip->proto     = NET_PROTO_UDP;
    ip->total_len = htons16((u16)(20 + plen));
    ip->id        = htons16((u16)(ip_frag_id++));
    for (int i = 0; i < 4; i++) {
        ip->src[i] = 0;
        ip->dst[i] = 0xFF;
    }
    ip->checksum  = htons16(inet_checksum(ip, 20));

    struct udp_hdr *u = (struct udp_hdr *)(frame + 34);
    u->sport    = htons16(68);
    u->dport    = htons16(67);
    u->length   = htons16((u16)(8 + plen));
    u->checksum = 0;                     /* zero checksum is legal for DHCP  */

    memcpy(frame + 42, payload, plen);
    e1000_send(frame, (u16)(42 + plen));
}

static void dhcp_handle(const struct ip4_hdr *ip, const u8 *payload, u16 len)
{
    (void)ip;
    if (len < sizeof(struct dhcp_hdr))
        return;
    const struct dhcp_hdr *d = (const struct dhcp_hdr *)payload;
    if (d->op != 2)
        return;
    if (ntohl32(d->xid) != dhcp_xid)
        return;

    const u8 *opts = (const u8 *)payload + sizeof(struct dhcp_hdr);
    u32 olen = (u32)len - sizeof(struct dhcp_hdr);

    u8 msg = 0;
    const u8 *router = NULL, *dnss = NULL;

    for (u32 i = 0; i < olen; ) {
        u8 code = opts[i];
        if (code == 255)
            break;
        if (code == 0 || i + 1 >= olen) {
            i++;
            continue;
        }
        u8 olen2 = opts[i + 1];
        if (i + 2 + olen2 > olen)
            break;
        if (code == 53 && olen2 == 1)
            msg = opts[i + 2];
        if (code == 3 && olen2 == 4)
            router = opts + i + 2;
        if (code == 6 && olen2 >= 4)
            dnss = opts + i + 2;
        i += 2 + olen2;
    }

    if (msg == DHCP_OFFER && net.dhcp_state == 1) {
        memcpy(net.ip, d->yiaddr, 4);
        memcpy(dhcp_requested, d->yiaddr, 4);
        if (router)
            memcpy(net.gw, router, 4);
        if (dnss)
            memcpy(net.dns, dnss, 4);
        memcpy(net.mask, SLIRP_MASK, 4);

        net.dhcp_state = 2;
        dhcp_started_ms = (u32)uptime_ms;
        dhcp_send(DHCP_REQUEST);
        printk("[dhcp] offer %u.%u.%u.%u, requesting\n",
                net.ip[0], net.ip[1], net.ip[2], net.ip[3]);
    }
    if (msg == DHCP_ACK && net.dhcp_state == 2) {
        net.dhcp_state = 3;
        printk("[dhcp] ack - network up\n");
    }
}

int net_dhcp_bound(void) { return net.dhcp_state == 3; }

/* ───────────────────────────────── DNS ─────────────────────────────────── */
static u16 dns_id;
static volatile u8 dns_result[4];
static volatile u8 dns_done;

static u16 dns_sport(void)
{
    return (u16)(50000 + 2000 + (dns_id & 0xFFF));
}

static void dns_send_query(const char *host)
{
    u8 payload[512];
    memset(payload, 0, sizeof(payload));
    struct dns_hdr *h = (struct dns_hdr *)payload;
    dns_id   = (u16)(uptime_ms & 0xFFFF);
    h->id    = htons16(dns_id);
    h->flags = htons16(0x0100);          /* recursion desired                */
    h->qd    = htons16(1);

    u8 *q = payload + 12;
    const char *p = host;
    while (*p) {
        const char *dot = p;
        while (*dot && *dot != '.')
            dot++;
        u32 lbl = (u32)(dot - p);
        if (lbl > 0 && lbl < 64) {
            *q++ = (u8)lbl;
            memcpy(q, p, lbl);
            q += lbl;
        }
        if (*dot == '.')
            dot++;
        else
            break;
        p = dot;
    }
    *q++ = 0;
    *q++ = 0; *q++ = 1;                  /* type A                           */
    *q++ = 0; *q++ = 1;                  /* class IN                         */

    udp_transmit(net.dns, dns_sport(), 53, payload, (u16)(q - payload), 0);
}

static void dns_handle(const u8 *payload, u16 len)
{
    if (len < 12 + 5)
        return;
    const struct dns_hdr *h = (const struct dns_hdr *)payload;
    if (ntohs16(h->id) != dns_id)
        return;
    u16 flags = ntohs16(h->flags);
    if (!(flags & 0x8000))
        return;                          /* not a response                   */
    if ((flags & 0x000F) != 0)
        return;                          /* rcode != NOERROR                 */
    if (ntohs16(h->an) == 0)
        return;

    u32 i = 12;
    while (i < len && payload[i] != 0)   /* question labels                  */
        i += payload[i] + 1;
    i += 5;                              /* NUL + qtype + qclass             */

    u16 nanswers = ntohs16(h->an);
    for (u16 n = 0; n < nanswers && i + 12 <= len; n++) {
        if (payload[i] == 0xC0)
            i += 2;
        else {
            while (i < len && payload[i] != 0)
                i += payload[i] + 1;
            i++;
        }
        u16 type  = (u16)((u16)payload[i] << 8 | payload[i + 1]);
        u16 rdlen = (u16)((u16)payload[i + 8] << 8 | payload[i + 9]);
        if (type == 1 && rdlen == 4 && i + 10 + 4 <= len) {
            dns_result[0] = payload[i + 10];
            dns_result[1] = payload[i + 11];
            dns_result[2] = payload[i + 12];
            dns_result[3] = payload[i + 13];
            dns_done = 1;
            return;
        }
        i += 10 + rdlen;
    }
}

int net_dns_lookup(const char *host, u8 out_ip[4])
{
    if (!net_dhcp_bound())
        return -1;
    if (host == NULL || out_ip == NULL)
        return -1;
    /* dotted-quad shortcut                                                */
    if (host[0] >= '0' && host[0] <= '9') {
        int a = 0, b = 0, c = 0, d = 0;
        const char *p = host;
        int ok = 1;
        for (int f = 0; f < 4; f++) {
            int v = 0, had = 0;
            while (*p >= '0' && *p <= '9') {
                v = v * 10 + (*p - '0');
                p++;
                had = 1;
            }
            if (!had || v > 255)
                { ok = 0; break; }
            if (f < 3) {
                if (*p != '.')
                    { ok = 0; break; }
                p++;
            }
            if (f == 0) a = v; else if (f == 1) b = v;
            else if (f == 2) c = v; else d = v;
        }
        if (ok && *p == '\0') {
            out_ip[0] = (u8)a; out_ip[1] = (u8)b;
            out_ip[2] = (u8)c; out_ip[3] = (u8)d;
            return 0;
        }
    }

    dns_done = 0;
    dns_send_query(host);

    u32 deadline = (u32)uptime_ms + 4000;
    while (!dns_done && uptime_ms < deadline) {
        net_poll_internal();
        for (volatile int k = 0; k < 2048; k++)
            ;
    }
    if (dns_done) {
        memcpy(out_ip, (const void *)dns_result, 4);
        return 0;
    }
    return -1;
}

/* ──────────────────────────────── TCP-lite ─────────────────────────────── */
#define NET_TCP_MAX   4

struct net_tcp {
    u8      state;
    u8      remote_ip[4];
    u16     lport, rport;
    u32     seq;                /* next byte we send                        */
    u32     ack;                /* next byte we expect                      */
    u8      rcvbuf[1792];
    u16     rcvlen, rcvpos;
    u32     born_ms;
};
static struct net_tcp tcp_slots[NET_TCP_MAX];

static u32 next_ephemeral = 40000;
static u16 tcp_new_port(void) { return (u16)(next_ephemeral++); }

static void tcp_transmit(struct net_tcp *c, u32 seq, u8 flags,
                         const void *data, u32 len)
{
    u8 seg[2048];
    u32 hdr = sizeof(struct tcp_hdr);
    if (len + hdr > sizeof(seg))
        return;

    struct tcp_hdr *t = (struct tcp_hdr *)seg;
    t->sport     = htons16(c->lport);
    t->dport     = htons16(c->rport);
    t->seq       = htonl32(seq);
    t->ack       = htonl32(c->ack);
    t->data_off  = 0x50;
    t->flags     = flags;
    t->window    = htons16(8192);
    t->checksum  = 0;
    t->urgent    = 0;
    memcpy(seg + hdr, data, len);

    t->checksum = htons16(proto_checksum(net.ip, c->remote_ip, NET_PROTO_TCP,
                                         seg, hdr + len));
    ip4_transmit(c->remote_ip, NET_PROTO_TCP, seg, (u16)(hdr + len));
}

static struct net_tcp *tcp_alloc(void)
{
    for (int i = 0; i < NET_TCP_MAX; i++)
        if (tcp_slots[i].state == NET_TCP_CLOSED)
            return &tcp_slots[i];
    return NULL;
}

net_tcp_h net_tcp_open(const u8 dest_ip[4], u16 dest_port)
{
    struct net_tcp *c = tcp_alloc();
    if (c == NULL)
        return NULL;

    memset(c, 0, sizeof(*c));
    memcpy(c->remote_ip, dest_ip, 4);
    c->rport   = dest_port;
    c->lport   = tcp_new_port();
    c->seq     = (u32)(uptime_ms * 7919u) | 1u;
    c->ack     = 0;
    c->state   = NET_TCP_SYN_SENT;
    c->born_ms = (u32)uptime_ms;

    tcp_transmit(c, c->seq, TCP_SYN, NULL, 0);
    return c;
}

int net_tcp_send(net_tcp_h c, const void *data, u32 len)
{
    if (c == NULL || c->state != NET_TCP_ESTABLISHED)
        return -1;
    if (len > 1400)
        return -1;

    tcp_transmit(c, c->seq, TCP_ACK | TCP_PSH, data, len);
    c->seq += len;
    return 0;
}

int net_tcp_recv(net_tcp_h c, void *buf, u32 cap)
{
    if (c == NULL)
        return -1;
    u32 avail = (u32)c->rcvlen - c->rcvpos;
    if (avail == 0)
        return 0;

    u32 take = (avail < cap) ? avail : cap;
    memcpy(buf, c->rcvbuf + c->rcvpos, take);
    c->rcvpos = (u16)(c->rcvpos + take);

    if (c->rcvpos == c->rcvlen) {
        c->rcvpos = c->rcvlen = 0;
        /* note: the segment was already ACKed in tcp_handle on arrival,
         * so no extra ACK here (would double-count the stream).          */
    }
    return (int)take;
}

int net_tcp_close(net_tcp_h c)
{
    if (c == NULL)
        return -1;
    if (c->state == NET_TCP_ESTABLISHED) {
        tcp_transmit(c, c->seq, TCP_FIN | TCP_ACK, NULL, 0);
        c->state = NET_TCP_FIN_SENT;
        c->born_ms = (u32)uptime_ms;
        return 0;
    }
    c->state = NET_TCP_CLOSED;
    return 0;
}

static void tcp_handle(const struct ip4_hdr *ip, const u8 *payload, u16 len)
{
    if (len < sizeof(struct tcp_hdr))
        return;
    const struct tcp_hdr *t = (const struct tcp_hdr *)payload;
    u32 hdr_len = (u32)(t->data_off >> 4) * 4;
    if (hdr_len < sizeof(struct tcp_hdr) || len < hdr_len)
        return;
    u32 data_len = (u32)len - hdr_len;

    u16 lport = ntohs16(t->dport);
    u16 rport = ntohs16(t->sport);
    u32 seq   = ntohl32(t->seq);

    for (int i = 0; i < NET_TCP_MAX; i++) {
        struct net_tcp *c = &tcp_slots[i];
        if (c->state == NET_TCP_CLOSED)
            continue;
        if (!ip_eq(ip->src, c->remote_ip))
            continue;
        if (lport != c->lport || rport != c->rport)
            continue;

        if (t->flags & TCP_RST) {
            c->state = NET_TCP_CLOSED;
            return;
        }

        if (c->state == NET_TCP_SYN_SENT) {
            if ((t->flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
                c->ack = seq + 1;
                c->state = NET_TCP_ESTABLISHED;
                tcp_transmit(c, c->seq, TCP_ACK, NULL, 0);
            }
            return;
        }

        if (c->state == NET_TCP_FIN_SENT) {
            if ((t->flags & TCP_ACK) && data_len == 0 &&
                seq == c->ack) {
                /* our FIN acknowledged: wait for peer FIN or close now    */
                c->state = NET_TCP_CLOSED;
                return;
            }
            if (t->flags & TCP_FIN) {
                c->ack = seq + 1;
                tcp_transmit(c, c->seq, TCP_ACK, NULL, 0);
                c->state = NET_TCP_CLOSED;
                return;
            }
            return;
        }

        if (c->state == NET_TCP_ESTABLISHED) {
            if (data_len > 0) {
                u32 room = sizeof(c->rcvbuf) - c->rcvlen;
                u32 take = (data_len < room) ? data_len : room;
                memcpy(c->rcvbuf + c->rcvlen, payload + hdr_len, take);
                c->rcvlen = (u16)(c->rcvlen + take);
            }
            if (t->flags & TCP_FIN) {
                c->ack += data_len + 1;
                tcp_transmit(c, c->seq, TCP_ACK, NULL, 0);
                c->state = NET_TCP_CLOSED;
                return;
            }
            if (data_len > 0) {
                /*
                 * Every buffered segment is ACKed immediately: slirp
                 * retransmits if it does not see the cumulative ACK fast.
                 * Drained-buffer accounting keeps rcvbuf simple.
                 */
                c->ack += data_len;
                tcp_transmit(c, c->seq, TCP_ACK, NULL, 0);
            }
            return;
        }
        return;
    }
}

/* ──────────────────────────────── ICMP ─────────────────────────────────── */
static volatile u8 ping_reply_seen;
#define PING_ID_HOST 0x1234

int net_ping(const u8 dest_ip[4])
{
    if (!net_dhcp_bound())
        return -1;

    ping_reply_seen = 0;

    u8 body[64];
    memset(body, 0, sizeof(body));
    struct icmp_hdr *ic = (struct icmp_hdr *)body;
    ic->type = 8;
    ic->code = 0;
    ic->checksum = 0;
    ic->id   = htons16(PING_ID_HOST);
    ic->seq  = htons16(1);
    for (int i = 0; i < 32; i++)
        body[8 + i] = (u8)('A' + i);
    ic->checksum = htons16(inet_checksum(body, 8 + 32));

    ip4_transmit(dest_ip, NET_PROTO_ICMP, body, 8 + 32);

    u32 deadline = (u32)uptime_ms + 3000;
    while (!ping_reply_seen && uptime_ms < deadline) {
        net_poll_internal();
        for (volatile int k = 0; k < 2048; k++)
            ;
    }
    return ping_reply_seen ? 0 : -1;
}

static void icmp_handle(const struct ip4_hdr *ip, const u8 *payload, u16 len)
{
    if (len < 8)
        return;
    const struct icmp_hdr *ic = (const struct icmp_hdr *)payload;

    if (ic->type == 8 && len <= 1480) {  /* echo request -> reply            */
        u8 reply[1480];
        memcpy(reply, payload, len);
        struct icmp_hdr *r = (struct icmp_hdr *)reply;
        r->type = 0;
        r->code = 0;
        r->checksum = 0;
        r->checksum = htons16(inet_checksum(reply, len));
        ip4_transmit(ip->src, NET_PROTO_ICMP, reply, len);
        return;
    }
    if (ic->type == 0 && ntohs16(ic->id) == PING_ID_HOST) {
        ping_reply_seen = 1;
        return;
    }
}

/* ───────────────────────────── firewall ────────────────────────────────── */
#define FW_RULES 16
struct fw_rule {
    u8  dir;
    u8  permit;
    u8  proto;
    u8  remote[4];
    u8  mask_bits;
    u16 port;
    u8  used;
};
static struct fw_rule fw_rules[FW_RULES];

static int fw_ip_match(const u8 b[4], const u8 rule_ip[4], u8 bits)
{
    if (bits == 0)
        return 1;
    if (bits > 32)
        bits = 32;
    for (int i = 0; i < 4; i++) {
        u8 in_rule_bits = (bits >= 8) ? 8 : bits;
        u8 m = (u8)(0xFFu << (8 - in_rule_bits)) | (u8)((1u << (8 - in_rule_bits)) - 1u);
        m = (u8)(m >> (8 - in_rule_bits) << (8 - in_rule_bits));
        if ((b[i] & m) != (rule_ip[i] & m))
            return 0;
        bits -= in_rule_bits;
        if (bits == 0)
            break;
    }
    return 1;
}

void net_firewall_add(int dir, int permit, u8 proto, const u8 remote_ip[4],
                      u8 mask_bits, u16 port)
{
    for (int i = 0; i < FW_RULES; i++) {
        if (fw_rules[i].used == 0) {
            fw_rules[i].dir      = (u8)dir;
            fw_rules[i].permit   = (u8)permit;
            fw_rules[i].proto    = proto;
            memcpy(fw_rules[i].remote, remote_ip, 4);
            fw_rules[i].mask_bits = mask_bits;
            fw_rules[i].port     = port;
            fw_rules[i].used     = 1;
            return;
        }
    }
}

int net_firewall(int dir, u8 proto, const u8 remote_ip[4], u16 port)
{
    /* management plane: DHCP + our DNS flow pass unconditionally           */
    if (dir == FW_DIR_IN && proto == NET_PROTO_UDP &&
        (port == 67 || port == 68))
        return FW_ALLOW;

    for (int i = 0; i < FW_RULES; i++) {
        if (!fw_rules[i].used || fw_rules[i].dir != dir)
            continue;
        if (fw_rules[i].proto != 0 && fw_rules[i].proto != proto)
            continue;
        if (!fw_ip_match(remote_ip, fw_rules[i].remote, fw_rules[i].mask_bits))
            continue;
        if (fw_rules[i].port != 0 && fw_rules[i].port != port)
            continue;
        return fw_rules[i].permit;
    }
    return dir == FW_DIR_IN ? FW_DENY : FW_ALLOW;   /* outbound open default */
}

void net_firewall_list(void)
{
    for (int i = 0; i < FW_RULES; i++) {
        if (!fw_rules[i].used)
            continue;
        printk("[fw  ] rule %d %s %s proto %u port %u\n", i,
                fw_rules[i].dir == FW_DIR_IN ? "in " : "out",
                fw_rules[i].permit == FW_ALLOW ? "allow" : "deny",
                fw_rules[i].proto, fw_rules[i].port);
    }
}

/* ─────────────────────────── receive demux ─────────────────────────────── */
static void arp_handle(const u8 *frame, u16 len)
{
    if (len < 14 + sizeof(struct arp_hdr))
        return;
    const struct arp_hdr *arp = (const struct arp_hdr *)(frame + 14);

    if (ntohs16(arp->ptype) != ETH_TYPE_IPV4 || arp->hlen != 6 || arp->plen != 4)
        return;
    if (!ip_eq(arp->tpa, net.ip))
        return;

    arp_cache_put(arp->spa, arp->sha);

    if (ntohs16(arp->oper) == 1) {
        u8 reply[42];
        memset(reply, 0, sizeof(reply));
        struct eth_hdr *eth = (struct eth_hdr *)reply;
        mac_copy(eth->dst, arp->sha);
        e1000_get_mac(eth->src);
        eth->type = htons16(ETH_TYPE_ARP);

        struct arp_hdr *out = (struct arp_hdr *)(reply + 14);
        out->htype = htons16(1);
        out->ptype = htons16(ETH_TYPE_IPV4);
        out->hlen  = 6;
        out->plen  = 4;
        out->oper  = htons16(2);
        e1000_get_mac(out->sha);
        memcpy(out->spa, net.ip, 4);
        mac_copy(out->tha, arp->sha);
        memcpy(out->tpa, arp->spa, 4);
        e1000_send(reply, 42);
    }
}

static u8 rx_frame_buf[2048];

static void ip4_handle(const u8 *frame, u16 len)
{
    if (len < 14 + 20)
        return;
    const struct ip4_hdr *ip = (const struct ip4_hdr *)(frame + 14);
    u16 total = ntohs16(ip->total_len);
    u16 plen;
    if (total <= 20 + (u16)(len - 14) && total > 20)
        plen = (u16)(total - 20);
    else
        plen = (u16)(len - 14 - 20);
    if (plen == 0)
        return;
    const u8 *payload = frame + 14 + 20;

    /* firewall: inbound gate                                              */
    u16 dport = 0;
    if (ip->proto == NET_PROTO_UDP && plen >= 4)
        dport = (u16)((u16)payload[2] << 8 | payload[3]);
    if (ip->proto == NET_PROTO_TCP && plen >= 4)
        dport = (u16)((u16)payload[2] << 8 | payload[3]);

    if (net_firewall(FW_DIR_IN, ip->proto, ip->src, dport) == FW_DENY)
        return;

    switch (ip->proto) {
    case NET_PROTO_ICMP:
        icmp_handle(ip, payload, plen);
        break;
    case NET_PROTO_UDP: {
        u16 sport = (u16)((u16)payload[0] << 8 | payload[1]);
        u16 dport2 = dport;
        if (dport2 == 67 || dport2 == 68)
            dhcp_handle(ip, payload + 8, (u16)(plen - 8));
        else if (sport == 53)
            dns_handle(payload + 8, (u16)(plen - 8));
        break;
    }
    case NET_PROTO_TCP:
        tcp_handle(ip, payload, plen);
        break;
    default:
        break;
    }
}

void net_poll_internal(void)
{
    if (e1000_present() == 0)
        return;

    for (;;) {
        u16 len = e1000_recv(rx_frame_buf, sizeof(rx_frame_buf));
        if (len < 14)
            break;

        const struct eth_hdr *eth = (const struct eth_hdr *)rx_frame_buf;
        u16 etype = ntohs16(eth->type);
        if (etype == ETH_TYPE_ARP)
            arp_handle(rx_frame_buf, len);
        else if (etype == ETH_TYPE_IPV4)
            ip4_handle(rx_frame_buf, len);
    }
}

void net_poll(void)
{
    net_poll_internal();

    if (net.dhcp_state == 1 && uptime_ms - dhcp_started_ms > 1000) {
        dhcp_started_ms = (u32)uptime_ms;
        dhcp_send(DHCP_DISCOVER);
    } else if (net.dhcp_state == 2 && uptime_ms - dhcp_started_ms > 4000) {
        /* no final ACK: accept the offered lease (slirp ACKs instantly)   */
        net.dhcp_state = 3;
    }
}

void net_init(void)
{
    memset(&net, 0, sizeof(net));
    e1000_get_mac(net.mac);
    memcpy(net.ip,  OUR_IP,     4);
    memcpy(net.gw,  SLIRP_GW,   4);
    memcpy(net.dns, SLIRP_DNS,  4);
    memcpy(net.mask, SLIRP_MASK, 4);
    memset((void *)arp_cache, 0, sizeof(arp_cache));
    memset((void *)tcp_slots, 0, sizeof(tcp_slots));

    e1000_init();
    if (e1000_present()) {
        net.dhcp_state = 1;
        dhcp_started_ms = (u32)uptime_ms;
        dhcp_xid = (u32)uptime_ms | 0x00010000u;
        printk("[net ] stack up, DHCP discover fired\n");
    } else {
        printk("[net ] no NIC: network disabled\n");
    }
}

/* ───────────────────────────── HTTP 1.1 ────────────────────────────────── */
net_tcp_h net_http_get(const char *host, const char *path)
{
    if (host == NULL || path == NULL)
        return NULL;
    if (!net_dhcp_bound())
        return NULL;

    u8 rip[4];
    if (net_dns_lookup(host, rip) != 0)
        return NULL;

    net_tcp_h c = net_tcp_open(rip, 80);
    if (c == NULL)
        return NULL;

    u32 deadline = (u32)uptime_ms + 4000;
    while (c->state == NET_TCP_SYN_SENT && uptime_ms < deadline)
        net_poll();

    if (c->state != NET_TCP_ESTABLISHED) {
        c->state = NET_TCP_CLOSED;
        return NULL;
    }

    char req[512];
    u32 n = 0;
    const char *m = "GET ";
    while (*m && n < sizeof(req) - 100)
        req[n++] = *m++;
    for (; *path && n < sizeof(req) - 100; path++)
        req[n++] = *path;
    m = " HTTP/1.1\r\nHost: ";
    while (*m && n < sizeof(req) - 100)
        req[n++] = *m++;
    for (; *host && n < sizeof(req) - 100; host++)
        req[n++] = *host++;
    m = "\r\nUser-Agent: BlackOS/1.0\r\nAccept: */*\r\nConnection: close\r\n\r\n";
    while (*m && n < sizeof(req) - 1)
        req[n++] = *m++;
    req[n] = '\0';

    if (net_tcp_send(c, req, n) != 0) {
        c->state = NET_TCP_CLOSED;
        return NULL;
    }
    return c;
}

int net_http_read(net_tcp_h c, void *buf, u32 cap)
{
    if (c == NULL)
        return 0;
    return net_tcp_recv(c, buf, cap);
}

void net_http_close(net_tcp_h c)
{
    if (c == NULL)
        return;
    net_tcp_close(c);

    u32 deadline = (u32)uptime_ms + 400;
    while (c->state == NET_TCP_FIN_SENT && uptime_ms < deadline)
        net_poll();
    c->state = NET_TCP_CLOSED;
}

/* ────────────────────────── JSON field decode ──────────────────────────── */
const char *net_json_field_str(const char *json, const char *key,
                               char *out, u32 cap)
{
    if (json == NULL || key == NULL || out == NULL || cap == 0)
        return NULL;

    char needle[64];
    u32 n = 0;
    needle[n++] = '"';
    while (*key && n < sizeof(needle) - 6)
        needle[n++] = *key++;
    needle[n++] = '"';
    needle[n] = '\0';

    const char *hit = json;
    for (;;) {
        hit = strstr(hit, needle);
        if (hit == NULL)
            return NULL;

        const char *colon = hit + strlen(needle);
        while (*colon == ' ' || *colon == '\t')
            colon++;
        if (*colon != ':') {
            hit += 1;                    /* skip false positive              */
            continue;
        }
        colon++;
        while (*colon == ' ')
            colon++;

        if (*colon == '"') {
            colon++;
            u32 i = 0;
            while (*colon && *colon != '"' && i < cap - 1)
                out[i++] = *colon++;
            out[i] = '\0';
            return out;
        }
        if (*colon == '-' || (*colon >= '0' && *colon <= '9')) {
            u32 i = 0;
            while ((*colon >= '0' && *colon <= '9' || *colon == '-' ||
                    *colon == '.') && i < cap - 1)
                out[i++] = *colon++;
            out[i] = '\0';
            return out;
        }
        hit += 1;
    }
}

int net_json_field_num(const char *json, const char *key, int *out)
{
    char buf[24];
    if (net_json_field_str(json, key, buf, sizeof(buf)) == NULL)
        return -1;

    int sign = 1, value = 0, digits = 0;
    const char *p = buf;
    if (*p == '-')
        { sign = -1; p++; }
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        p++;
        digits++;
    }
    if (digits == 0)
        return -1;
    if (out)
        *out = sign * value;
    return 0;
}
