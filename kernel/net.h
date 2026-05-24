#ifndef NET_H
#define NET_H

#include <stdint.h>

/*
 * VESPER OS – Network stack (Ethernet / ARP / IPv4 / UDP / DHCP)
 *
 * All multi-byte fields that travel on the wire are stored in network
 * byte order (big-endian).  The helpers put16be / put32be / get16be /
 * get32be convert between the wire representation and the host uint16_t /
 * uint32_t values used internally.
 *
 * IP addresses are stored as plain uint32_t values with the most
 * significant byte equal to the first octet (e.g. 192.168.1.1 →
 * 0xC0A80101).  This matches the result of get32be() on a network packet
 * and allows printing as (ip >> 24) & 0xFF, etc.
 */

/* Ethernet protocol types */
#define ETHERTYPE_IP   0x0800u
#define ETHERTYPE_ARP  0x0806u

/* IP protocol numbers */
#define IP_PROTO_UDP   17u

/* Well-known UDP port numbers */
#define PORT_DHCP_SERVER  67u
#define PORT_DHCP_CLIENT  68u

/* Ethernet / IP header sizes */
#define ETH_HDR_LEN  14u
#define IP_HDR_LEN   20u
#define UDP_HDR_LEN   8u

/* Broadcast MAC address */
extern const uint8_t mac_broadcast[6];

/* -------------------------------------------------------------------------
 * Network configuration (populated by DHCP or static assignment)
 * ---------------------------------------------------------------------- */
typedef struct {
    uint8_t  mac[6];      /* NIC hardware address                    */
    uint32_t ip;          /* Host IP address     (0 until configured) */
    uint32_t netmask;     /* Subnet mask         (0 until configured) */
    uint32_t gateway;     /* Default gateway     (0 until configured) */
    uint32_t dns;         /* Primary DNS server  (0 until configured) */
    int      configured;  /* 1 = valid DHCP lease obtained            */
} net_config_t;

extern net_config_t net_config;

/* -------------------------------------------------------------------------
 * ARP cache
 * ---------------------------------------------------------------------- */
#define ARP_CACHE_SIZE  8u

typedef struct {
    uint32_t ip;
    uint8_t  mac[6];
    int      valid;
} arp_entry_t;

/* Return a pointer to the cached MAC for @ip, or NULL if not present */
const uint8_t *arp_lookup(uint32_t ip);

/* Insert or refresh an ARP cache entry */
void arp_cache_update(uint32_t ip, const uint8_t *mac_addr);

/* Send a gratuitous ARP request for @target_ip */
void arp_request(uint32_t target_ip);

/* Print the ARP cache to the VGA console */
void net_print_arp_cache(void);

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/* Initialise the network layer (call after rtl8139_init) */
void net_init(void);

/*
 * net_rx – dispatch an incoming Ethernet frame.
 * Called from the RTL8139 IRQ handler; must be safe to call from IRQ context.
 */
void net_rx(const uint8_t *frame, uint16_t len);

/* Send a raw Ethernet frame (dst MAC, ethertype, payload) */
void net_send_eth(const uint8_t dst_mac[6], uint16_t ethertype,
                  const uint8_t *payload, uint16_t len);

/* Build and transmit an IPv4 UDP datagram */
void net_send_udp(uint32_t dst_ip,
                  uint16_t src_port, uint16_t dst_port,
                  const uint8_t *data, uint16_t data_len);

/* -------------------------------------------------------------------------
 * DHCP client
 * ---------------------------------------------------------------------- */

/*
 * dhcp_discover – broadcast DHCPDISCOVER then complete the DORA handshake.
 *
 * @timeout_ticks : how many 100 Hz timer ticks to wait for an ACK.
 *
 * The function returns only after success or timeout.  While waiting it
 * executes HLT so hardware IRQs (timer + NIC) can fire normally.
 * Interrupts must be enabled before calling this function.
 *
 * Returns 1 if net_config was populated, 0 on timeout.
 */
int dhcp_discover(uint32_t timeout_ticks);

/* Called from net_rx when a UDP datagram arrives on DHCP client port 68 */
void dhcp_rx(const uint8_t *payload, uint16_t len);

#endif /* NET_H */
