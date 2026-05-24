#include "net.h"
#include "rtl8139.h"
#include "string.h"
#include "timer.h"
#include "vga.h"

/* -------------------------------------------------------------------------
 * Module-level globals
 * ---------------------------------------------------------------------- */
const uint8_t mac_broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

net_config_t net_config;

/* -------------------------------------------------------------------------
 * Wire-order byte helpers (do not depend on CPU endianness)
 * ---------------------------------------------------------------------- */
static uint16_t get16be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

static void put16be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >>  8);
    p[3] = (uint8_t)v;
}

/* -------------------------------------------------------------------------
 * IPv4 header checksum (1's-complement sum over header bytes)
 * ---------------------------------------------------------------------- */
static uint16_t ip_checksum(const uint8_t *buf, uint16_t len)
{
    uint32_t sum = 0u;
    while (len > 1u) {
        sum += (uint32_t)get16be(buf);
        buf += 2;
        len = (uint16_t)(len - 2u);
    }
    if (len) {
        sum += (uint32_t)buf[0] << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

/* -------------------------------------------------------------------------
 * ARP cache
 * ---------------------------------------------------------------------- */
static arp_entry_t arp_cache[ARP_CACHE_SIZE];
static uint8_t     arp_evict_idx = 0u;  /* next slot to evict (round-robin) */

const uint8_t *arp_lookup(uint32_t ip)
{
    for (uint8_t i = 0u; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            return arp_cache[i].mac;
        }
    }
    return (const uint8_t *)0;
}

void arp_cache_update(uint32_t ip, const uint8_t *mac_addr)
{
    /* Refresh existing entry */
    for (uint8_t i = 0u; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(arp_cache[i].mac, mac_addr, 6u);
            return;
        }
    }
    /* Insert into next eviction slot */
    uint8_t idx = arp_evict_idx;
    arp_evict_idx = (uint8_t)((arp_evict_idx + 1u) % ARP_CACHE_SIZE);
    arp_cache[idx].ip    = ip;
    arp_cache[idx].valid = 1;
    memcpy(arp_cache[idx].mac, mac_addr, 6u);
}

void net_print_arp_cache(void)
{
    int found = 0;
    for (uint8_t i = 0u; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            continue;
        }
        found = 1;
        uint32_t ip = arp_cache[i].ip;
        vga_printf("  %u.%u.%u.%u  ->  %02x:%02x:%02x:%02x:%02x:%02x\n",
                   (ip >> 24) & 0xFFu,
                   (ip >> 16) & 0xFFu,
                   (ip >>  8) & 0xFFu,
                    ip        & 0xFFu,
                   arp_cache[i].mac[0], arp_cache[i].mac[1],
                   arp_cache[i].mac[2], arp_cache[i].mac[3],
                   arp_cache[i].mac[4], arp_cache[i].mac[5]);
    }
    if (!found) {
        vga_puts("  (ARP cache is empty)\n");
    }
}

/* -------------------------------------------------------------------------
 * ARP packet layout (28 bytes for IPv4/Ethernet)
 * ---------------------------------------------------------------------- */
typedef struct {
    uint8_t  htype[2];  /* hardware type: 1 = Ethernet       */
    uint8_t  ptype[2];  /* protocol type: 0x0800 = IPv4      */
    uint8_t  hlen;      /* hardware address length: 6        */
    uint8_t  plen;      /* protocol address length: 4        */
    uint8_t  oper[2];   /* operation: 1 = request, 2 = reply */
    uint8_t  sha[6];    /* sender hardware (MAC) address      */
    uint8_t  spa[4];    /* sender protocol (IP)  address      */
    uint8_t  tha[6];    /* target hardware (MAC) address      */
    uint8_t  tpa[4];    /* target protocol (IP)  address      */
} __attribute__((packed)) arp_pkt_t;

#define ARP_REQUEST 1u
#define ARP_REPLY   2u

void arp_request(uint32_t target_ip)
{
    uint8_t buf[sizeof(arp_pkt_t)];
    arp_pkt_t *a = (arp_pkt_t *)buf;
    put16be(a->htype, 1u);
    put16be(a->ptype, 0x0800u);
    a->hlen = 6u;
    a->plen = 4u;
    put16be(a->oper, ARP_REQUEST);
    memcpy(a->sha, net_config.mac, 6u);
    put32be(a->spa, net_config.ip);
    memset(a->tha, 0, 6u);
    put32be(a->tpa, target_ip);
    net_send_eth(mac_broadcast, ETHERTYPE_ARP, buf, (uint16_t)sizeof(buf));
}

static void arp_send_reply(const uint8_t *dst_mac, uint32_t src_ip,
                           uint32_t target_ip)
{
    uint8_t buf[sizeof(arp_pkt_t)];
    arp_pkt_t *a = (arp_pkt_t *)buf;
    put16be(a->htype, 1u);
    put16be(a->ptype, 0x0800u);
    a->hlen = 6u;
    a->plen = 4u;
    put16be(a->oper, ARP_REPLY);
    memcpy(a->sha, net_config.mac, 6u);
    put32be(a->spa, net_config.ip);
    memcpy(a->tha, dst_mac, 6u);
    put32be(a->tpa, target_ip);
    (void)src_ip;
    net_send_eth(dst_mac, ETHERTYPE_ARP, buf, (uint16_t)sizeof(buf));
}

/* -------------------------------------------------------------------------
 * DHCP state machine
 *
 * States:
 *   0 – idle
 *   1 – DISCOVER sent, waiting for OFFER
 *   2 – OFFER received, REQUEST not yet sent
 *   3 – REQUEST sent, waiting for ACK
 *   4 – BOUND (ACK received, net_config populated)
 * ---------------------------------------------------------------------- */
#define DHCP_XID  0xCAFEBABEu   /* fixed transaction ID */

/* DHCP message type option values */
#define DHCP_DISCOVER  1u
#define DHCP_OFFER     2u
#define DHCP_REQUEST   3u
#define DHCP_ACK       5u

/*
 * DHCP packet: 236 bytes of fixed header + up to 312 bytes of options.
 * We never build options that require more than ~30 bytes.
 */
#define DHCP_HDR_SIZE      236u
#define DHCP_OPTIONS_SIZE  64u
#define DHCP_PKT_SIZE      (DHCP_HDR_SIZE + DHCP_OPTIONS_SIZE)

static volatile uint8_t  dhcp_state       = 0u;
static volatile uint32_t dhcp_offered_ip  = 0u;
static volatile uint32_t dhcp_server_ip   = 0u;

/*
 * Build the fixed portion of a DHCP packet and append the minimal
 * option set required for DISCOVER or REQUEST.
 */
static void dhcp_build(uint8_t *pkt, uint8_t msg_type)
{
    memset(pkt, 0, DHCP_PKT_SIZE);

    pkt[0] = 1u;                      /* op = BOOTREQUEST          */
    pkt[1] = 1u;                      /* htype = Ethernet          */
    pkt[2] = 6u;                      /* hlen = 6 bytes            */
    put32be(pkt + 4,  DHCP_XID);      /* transaction ID            */
    put16be(pkt + 10, 0x8000u);       /* flags: broadcast reply    */
    /* ciaddr, yiaddr, siaddr, giaddr all zero for DISCOVER/REQUEST */
    memcpy(pkt + 28, net_config.mac, 6u);  /* chaddr (MAC address) */

    /* DHCP magic cookie */
    uint8_t *opt = pkt + DHCP_HDR_SIZE;
    opt[0] = 0x63u; opt[1] = 0x82u; opt[2] = 0x53u; opt[3] = 0x63u;
    opt += 4;

    /* Option 53: DHCP Message Type */
    opt[0] = 53u; opt[1] = 1u; opt[2] = msg_type;
    opt += 3;

    if (msg_type == DHCP_REQUEST) {
        /* Option 50: Requested IP Address */
        opt[0] = 50u; opt[1] = 4u;
        put32be(opt + 2, dhcp_offered_ip);
        opt += 6;
        /* Option 54: Server Identifier */
        opt[0] = 54u; opt[1] = 4u;
        put32be(opt + 2, dhcp_server_ip);
        opt += 6;
    }

    /* Option 55: Parameter Request List (subnet mask, router, DNS) */
    opt[0] = 55u; opt[1] = 3u;
    opt[2] = 1u; opt[3] = 3u; opt[4] = 6u;
    opt += 5;

    /* Option 255: End */
    opt[0] = 255u;
}

void dhcp_rx(const uint8_t *pkt, uint16_t len)
{
    if (len < DHCP_HDR_SIZE + 4u) {
        return;
    }

    /* Verify this is a BOOTREPLY with our transaction ID */
    if (pkt[0] != 2u) {
        return;
    }
    if (get32be(pkt + 4) != DHCP_XID) {
        return;
    }

    uint32_t yiaddr = get32be(pkt + 16);

    /* Parse options (skip 4-byte magic cookie) */
    const uint8_t *opt = pkt + DHCP_HDR_SIZE + 4u;
    const uint8_t *end = pkt + len;

    uint8_t  msg_type  = 0u;
    uint32_t server_id = 0u;
    uint32_t subnet    = 0u;
    uint32_t router    = 0u;
    uint32_t dns_ip    = 0u;

    while (opt < end && opt[0] != 255u) {
        if (opt[0] == 0u) {         /* pad byte */
            opt++;
            continue;
        }
        if (opt + 2u > end) {
            break;
        }
        uint8_t tag  = opt[0];
        uint8_t olen = opt[1];
        opt += 2;
        if (opt + olen > end) {
            break;
        }
        switch (tag) {
        case 53u: msg_type  = opt[0];          break;
        case 54u: server_id = get32be(opt);    break;
        case  1u: subnet    = get32be(opt);    break;
        case  3u: router    = get32be(opt);    break;
        case  6u: dns_ip    = get32be(opt);    break;
        default:                               break;
        }
        opt += olen;
    }

    if (msg_type == DHCP_OFFER && dhcp_state == 1u) {
        dhcp_offered_ip = yiaddr;
        dhcp_server_ip  = server_id;
        dhcp_state      = 2u;           /* trigger REQUEST in dhcp_discover() */
    } else if (msg_type == DHCP_ACK && dhcp_state == 3u) {
        net_config.ip         = yiaddr;
        net_config.netmask    = subnet  ? subnet  : 0xFFFFFF00u;
        net_config.gateway    = router;
        net_config.dns        = dns_ip;
        net_config.configured = 1;
        dhcp_state            = 4u;     /* BOUND */
    }
}

int dhcp_discover(uint32_t timeout_ticks)
{
    static uint8_t dhcp_pkt[DHCP_PKT_SIZE];

    dhcp_state      = 0u;
    dhcp_offered_ip = 0u;
    dhcp_server_ip  = 0u;

    /* Send initial DISCOVER */
    dhcp_build(dhcp_pkt, DHCP_DISCOVER);
    net_send_udp(0xFFFFFFFFu,
                 PORT_DHCP_CLIENT, PORT_DHCP_SERVER,
                 dhcp_pkt, DHCP_PKT_SIZE);
    dhcp_state = 1u;

    uint32_t deadline = timer_get_ticks() + timeout_ticks;

    while (timer_get_ticks() < deadline) {
        if (dhcp_state == 2u) {
            /* OFFER received – send REQUEST */
            dhcp_build(dhcp_pkt, DHCP_REQUEST);
            net_send_udp(0xFFFFFFFFu,
                         PORT_DHCP_CLIENT, PORT_DHCP_SERVER,
                         dhcp_pkt, DHCP_PKT_SIZE);
            dhcp_state = 3u;
        }
        if (dhcp_state == 4u) {
            return 1;   /* BOUND */
        }
        /* HLT allows timer and NIC IRQs to fire; wakes on next interrupt */
        __asm__ volatile ("hlt");
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * ARP frame reception
 * ---------------------------------------------------------------------- */
static void arp_rx_frame(const uint8_t *payload, uint16_t len)
{
    if (len < (uint16_t)sizeof(arp_pkt_t)) {
        return;
    }
    const arp_pkt_t *a     = (const arp_pkt_t *)payload;
    uint16_t         oper  = get16be(a->oper);
    uint32_t sender_ip     = get32be(a->spa);
    uint32_t target_ip     = get32be(a->tpa);

    arp_cache_update(sender_ip, a->sha);

    if (oper == ARP_REQUEST && net_config.ip != 0u &&
        target_ip == net_config.ip) {
        arp_send_reply(a->sha, target_ip, sender_ip);
    }
}

/* -------------------------------------------------------------------------
 * IPv4 / UDP reception
 * ---------------------------------------------------------------------- */
static void ipv4_rx(const uint8_t *ip_hdr, uint16_t len)
{
    if (len < IP_HDR_LEN) {
        return;
    }
    uint8_t  proto   = ip_hdr[9];
    if (proto != IP_PROTO_UDP) {
        return;
    }

    const uint8_t *udp     = ip_hdr + IP_HDR_LEN;
    uint16_t       udp_len = (uint16_t)(len - IP_HDR_LEN);

    if (udp_len < UDP_HDR_LEN) {
        return;
    }

    uint16_t dst_port  = get16be(udp + 2u);
    uint16_t data_len  = get16be(udp + 4u);

    if (data_len < UDP_HDR_LEN || data_len > udp_len) {
        return;
    }

    const uint8_t *data    = udp + UDP_HDR_LEN;
    uint16_t payload_len   = (uint16_t)(data_len - UDP_HDR_LEN);

    if (dst_port == PORT_DHCP_CLIENT) {
        dhcp_rx(data, payload_len);
    }
}

/* -------------------------------------------------------------------------
 * net_rx – dispatch an incoming Ethernet frame
 * ---------------------------------------------------------------------- */
void net_rx(const uint8_t *frame, uint16_t len)
{
    if (len < ETH_HDR_LEN) {
        return;
    }
    uint16_t       ethertype   = get16be(frame + 12u);
    const uint8_t *payload     = frame + ETH_HDR_LEN;
    uint16_t       payload_len = (uint16_t)(len - ETH_HDR_LEN);

    /* Learn source MAC → IP mapping from ARP frames */
    if (ethertype == ETHERTYPE_ARP) {
        arp_rx_frame(payload, payload_len);
    } else if (ethertype == ETHERTYPE_IP) {
        /* Also cache the sender MAC from the Ethernet header */
        if (payload_len >= IP_HDR_LEN) {
            uint32_t src_ip = get32be(payload + 12u);
            arp_cache_update(src_ip, frame + 6u);
        }
        ipv4_rx(payload, payload_len);
    }
}

/* -------------------------------------------------------------------------
 * Ethernet transmission
 * ---------------------------------------------------------------------- */

/* Single static TX frame buffer (not re-entrant; called from main thread) */
static uint8_t tx_eth_frame[ETH_HDR_LEN + IP_HDR_LEN + UDP_HDR_LEN + 548u];

void net_send_eth(const uint8_t dst_mac[6], uint16_t ethertype,
                  const uint8_t *payload, uint16_t payload_len)
{
    uint16_t total = (uint16_t)(ETH_HDR_LEN + payload_len);
    if (total > (uint16_t)sizeof(tx_eth_frame)) {
        return;
    }
    memcpy(tx_eth_frame,       dst_mac,          6u);
    memcpy(tx_eth_frame + 6u,  net_config.mac,   6u);
    put16be(tx_eth_frame + 12u, ethertype);
    memcpy(tx_eth_frame + ETH_HDR_LEN, payload, payload_len);
    rtl8139_send(tx_eth_frame, total);
}

/* -------------------------------------------------------------------------
 * IPv4 + UDP transmission
 * ---------------------------------------------------------------------- */
static uint8_t tx_ip_buf[IP_HDR_LEN + UDP_HDR_LEN + 548u];

void net_send_udp(uint32_t dst_ip,
                  uint16_t src_port, uint16_t dst_port,
                  const uint8_t *data, uint16_t data_len)
{
    uint16_t udp_len = (uint16_t)(UDP_HDR_LEN + data_len);
    uint16_t ip_len  = (uint16_t)(IP_HDR_LEN  + udp_len);

    if (ip_len > (uint16_t)sizeof(tx_ip_buf)) {
        return;
    }

    /* UDP header */
    uint8_t *udp = tx_ip_buf + IP_HDR_LEN;
    put16be(udp + 0u, src_port);
    put16be(udp + 2u, dst_port);
    put16be(udp + 4u, udp_len);
    put16be(udp + 6u, 0u);              /* checksum optional for IPv4 */
    memcpy(udp + UDP_HDR_LEN, data, data_len);

    /* IPv4 header */
    uint8_t *ip = tx_ip_buf;
    memset(ip, 0, IP_HDR_LEN);
    ip[0] = 0x45u;                      /* version=4, IHL=5 dwords      */
    put16be(ip + 2u,  ip_len);
    put16be(ip + 6u,  0x4000u);         /* DF flag, fragment offset = 0 */
    ip[8] = 64u;                        /* TTL                          */
    ip[9] = IP_PROTO_UDP;
    put32be(ip + 12u, net_config.ip);
    put32be(ip + 16u, dst_ip);
    put16be(ip + 10u, ip_checksum(ip, IP_HDR_LEN));  /* header checksum */

    /* Determine destination MAC */
    const uint8_t *dst_mac;
    if (dst_ip == 0xFFFFFFFFu || net_config.ip == 0u) {
        dst_mac = mac_broadcast;
    } else {
        uint32_t next_hop = dst_ip;
        if (net_config.netmask != 0u &&
            (dst_ip & net_config.netmask) != (net_config.ip & net_config.netmask)) {
            next_hop = net_config.gateway;
        }
        dst_mac = arp_lookup(next_hop);
        if (!dst_mac) {
            dst_mac = mac_broadcast;    /* fallback until ARP resolves */
        }
    }

    net_send_eth(dst_mac, ETHERTYPE_IP, tx_ip_buf, ip_len);
}

/* -------------------------------------------------------------------------
 * Initialisation
 * ---------------------------------------------------------------------- */
void net_init(void)
{
    memset(&net_config,  0, sizeof(net_config));
    memset(arp_cache,    0, sizeof(arp_cache));
    arp_evict_idx = 0u;
    rtl8139_get_mac(net_config.mac);
}
